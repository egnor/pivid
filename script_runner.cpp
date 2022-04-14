#include "script_runner.h"

#include <optional>

#include "display_output.h"
#include "frame_loader.h"
#include "frame_player.h"
#include "logging_policy.h"

namespace pivid {

namespace {

auto const& runner_logger() {
    static const auto logger = make_logger("runner");
    return logger;
}

bool matches_display(std::string const& conn, DisplayScreen const& disp) {
    return conn == disp.connector || (conn == "*" && disp.display_detected);
}

bool matches_mode(ScriptScreen const& scr, DisplayMode const& mode) {
    if (scr.display_mode.x < 0 || scr.display_mode.y < 0 || scr.display_hz < 0)
        return (mode.nominal_hz == 0);
    return (
        (!scr.display_mode.x || scr.display_mode.x == mode.size.x) &&
        (!scr.display_mode.y || scr.display_mode.y == mode.size.y) &&
        (!scr.display_hz || scr.display_hz == mode.nominal_hz)
    );
}

class ScriptRunnerDef : public ScriptRunner {
  public:
    virtual ScriptStatus update(
        Script const& script, std::shared_ptr<SyncFlag> signal
    ) final {
        auto const now = cx.sys->clock();
        double const t0 = script.time_is_relative ? cx.start_time : 0.0;
        DEBUG(logger, "UPDATE {} (t={:.3f}s)", abbrev_realtime(now), now - t0);

        std::vector<DisplayScreen> display_screens;
        for (auto const& [connector, script_screen] : script.screens) {
            auto *output = &outputs[connector];
            output->defined = true;

            if (output->player && matches_mode(script_screen, output->mode)) {
                DEBUG(logger, "  [{}] {}", output->name, debug(output->mode));
            } else {
                if (display_screens.empty())
                    display_screens = cx.driver->scan_screens();

                std::string display_name;
                uint32_t display_id = 0;
                DisplayMode display_mode = {};
                for (auto const& display : display_screens) {
                    if (matches_display(connector, display)) {
                        display_name = display.connector;
                        display_id = display.id;
                        if (matches_mode(script_screen, display.active_mode)) {
                            display_mode = display.active_mode;
                        } else {
                            for (auto const& mode : display.modes) {
                                if (matches_mode(script_screen, mode)) {
                                    display_mode = mode;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }

                if (display_id == 0) {
                    logger->error("Connector not found: \"{}\"", connector);
                    continue;
                }

                if (!matches_mode(script_screen, display_mode)) {
                    logger->error(
                        "Mode not found: {} {}x{} {}Hz", display_name,
                        script_screen.display_mode.x,
                        script_screen.display_mode.y,
                        script_screen.display_hz
                    );
                    continue;
                }

                DEBUG(logger, "  [{}] + {}", display_name, debug(display_mode));
                output->name = display_name;
                output->mode = display_mode;
                output->player.reset();
                output->player = cx.player_f(display_id, display_mode);
            }

            if (!output->mode.nominal_hz) {
                output->player->set_timeline({}, signal);
                continue;
            }

            ASSERT(output->mode.actual_hz() > 0);
            double const script_hz = script_screen.update_hz;
            double const hz = script_hz ? script_hz : output->mode.actual_hz();
            double const next_output_time = std::ceil(now * hz) / hz;

            FramePlayer::Timeline timeline;
            for (size_t li = 0; li < script_screen.layers.size(); ++li) {
                auto const& layer = script_screen.layers[li];
                auto const& file = canonicalize_file(layer.media.file);
                auto* input = &inputs[file];
                DEBUG(logger, "    \"{}\"", file);

                Interval const buffer{now - t0, now - t0 + layer.media.buffer};
                auto const buffer_range = layer.media.play.range(buffer);
                TRACE(logger, "      want {}", debug(buffer_range));
                input->request.insert(buffer_range);

                if (!input->content) {
                    if (!input->loader) continue;
                    input->content = input->loader->content();
                }
                TRACE(logger, "      have {}", debug(input->content->have));

                for (
                    double t = next_output_time;
                    t < now + script.main_buffer;
                    t += 1.0 / hz
                ) {
                    double const rt = t - t0;
                    auto const get = [rt](BezierSpline const& bez, double def) {
                        return bez.value(rt).value_or(def);
                    };

                    auto const media_t = get(layer.media.play, -1);
                    if (media_t < 0) {
                        TRACE(
                            logger, "      {:+.3f}s m={:.3f}s before start!",
                            t - now, media_t
                        );
                        continue;
                    }

                    if (!input->content->have.contains(media_t)) {
                        TRACE(
                            logger, "      {:+.3f}s m={:.3f}s not loaded!",
                            t - now, media_t
                        );
                        continue;
                    }

                    auto frame_it = input->content->frames.upper_bound(media_t);
                    if (frame_it == input->content->frames.begin()) {
                        TRACE(
                            logger, "      {:+.3f}s m={:.3f}s no frame!",
                            t - now, media_t
                        );
                        continue;
                    }

                    --frame_it;
                    auto const frame_t = frame_it->first;
                    auto const image_size = frame_it->second->size();
                    auto* out = &timeline[t].emplace_back();
                    out->from_xy.x = get(layer.from_xy.x, 0);
                    out->from_xy.y = get(layer.from_xy.y, 0);
                    out->from_size.x = get(layer.from_size.x, image_size.x);
                    out->from_size.y = get(layer.from_size.y, image_size.y);
                    out->to_xy.x = get(layer.to_xy.x, 0);
                    out->to_xy.y = get(layer.to_xy.y, 0);
                    out->to_size.x = get(layer.to_size.x, image_size.x);
                    out->to_size.y = get(layer.to_size.y, image_size.y);
                    out->opacity = get(layer.opacity, 1);
                    TRACE(
                        logger, "        {:+.3f}s m{:.3f} f{:.3f} {}",
                        t - now, media_t, frame_t, debug(*out)
                    );

                    out->image = frame_it->second;  // Not in TRACE above
                }
            }

            output->player->set_timeline(std::move(timeline), signal);
        }

        for (const auto& script_standby : script.standbys) {
            auto const& file = canonicalize_file(script_standby.file);
            auto* input = &inputs[file];
            TRACE(logger, "  standby \"{}\"", file);

            Interval const buffer{now - t0, now - t0 + script_standby.buffer};
            auto const buffer_range = script_standby.play.range(buffer);
            TRACE(logger, "    want {}", debug(buffer_range));
            input->request.insert(buffer_range);
        }

        ScriptStatus status;
        status.update_time = now;

        auto input_it = inputs.begin();
        while (input_it != inputs.end()) {
            auto *input = &input_it->second;
            if (input->request.empty()) {
                if (input->loader) {
                    DEBUG(logger, "  closing \"{}\"", input_it->first);
                } else {
                    TRACE(logger, "  unused \"{}\"", input_it->first);
                }
                input_it = inputs.erase(input_it);
            } else {
                if (input->loader) {
                    TRACE(logger, "  \"{}\"", input_it->first);
                } else {
                    DEBUG(logger, "  opening \"{}\"", input_it->first);
                    input->loader = cx.loader_f(input_it->first);
                }

                if (input->content && input->content->eof)
                    status.media_eof[input_it->first] = *input->content->eof;

                TRACE(logger, "    request {}", debug(input->request));
                input->loader->set_request(input->request, signal);
                input->request = {};
                input->content = {};
                ++input_it;
            }
        }

        auto output_it = outputs.begin();
        while (output_it != outputs.end()) {
            if (!output_it->second.defined) {
                DEBUG(logger, "  [{}] stopping", output_it->second.name);
                output_it = outputs.erase(output_it);
            } else {
                status.screen_mode[output_it->first] = output_it->second.mode;
                output_it->second.defined = false;
                ++output_it;
            }
        }

        TRACE(logger, "  update done");
        return status;
    }

    void init(ScriptContext c) {
        CHECK_ARG(c.driver, "No driver for ScriptRunner");
        cx = std::move(c);

        if (!cx.sys)
            cx.sys = global_system();

        if (!cx.loader_f) {
            cx.loader_f = [this](std::string const& file) {
                return start_frame_loader(cx.driver, file, cx.sys);
            };
        }

        if (!cx.player_f) {
            cx.player_f = [this](uint32_t id, DisplayMode const& m) {
                return start_frame_player(cx.driver, id, m, cx.sys);
            };
        }

        if (cx.root_dir.empty()) cx.root_dir = "/";
        if (cx.file_base.empty()) cx.file_base = ".";
        cx.root_dir = cx.sys->realpath(cx.root_dir).ex(cx.root_dir);
        cx.file_base = cx.sys->realpath(cx.file_base).ex(cx.file_base);
        if (!S_ISDIR(cx.sys->stat(cx.file_base).ex(cx.file_base).st_mode)) {
            auto const slash = cx.file_base.rfind('/');
            if (slash >= 1) cx.file_base.resize(slash);
        }

        if (!cx.root_dir.ends_with('/')) cx.root_dir += "/";
        if (!cx.file_base.ends_with('/')) cx.file_base += "/";
    }

  private:
    struct Input {
        std::unique_ptr<FrameLoader> loader;
        IntervalSet request;
        std::optional<FrameLoader::Content> content;
    };

    struct Output {
        std::string name;
        XY<int> size;
        DisplayMode mode;
        std::unique_ptr<FramePlayer> player;
        bool defined = false;
    };

    std::shared_ptr<log::logger> const logger = runner_logger();
    ScriptContext cx = {};

    std::map<std::string, Input> inputs;
    std::map<std::string, Output> outputs;
    std::map<std::string, std::string> canonicalize_cache;

    std::string const& canonicalize_file(std::string const& spec) {
        CHECK_ARG(!spec.empty(), "Empty filename");
        auto it = canonicalize_cache.find(spec);
        if (it == canonicalize_cache.end()) {
            auto canonical = cx.sys->realpath(
                spec.starts_with('/') ? cx.root_dir + spec : cx.file_base + spec
            ).ex(spec);

            CHECK_ARG(
                canonical.starts_with(cx.root_dir),
                "\"{}\" ({}) outside root ({})", spec, canonical, cx.root_dir
            );

            it = canonicalize_cache.insert({spec, std::move(canonical)}).first;
        }

        return it->second;
    }
};

}  // anonymous namespace

std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext cx) {
    auto runner = std::make_unique<ScriptRunnerDef>();
    runner->init(std::move(cx));
    return runner;
}

}  // namespace pivid
