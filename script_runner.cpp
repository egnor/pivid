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

bool matches_mode(ScriptScreen const& screen, DisplayMode const& mode) {
    return (
        (!screen.display_mode.x || screen.display_mode.x == mode.size.x) &&
        (!screen.display_mode.y || screen.display_mode.y == mode.size.y) &&
        (!screen.display_hz || screen.display_hz == mode.nominal_hz)
    );
}

class ScriptRunnerDef : public ScriptRunner {
  public:
    virtual ScriptStatus update(
        Script const& script, std::shared_ptr<SyncFlag> signal
    ) final {
        auto const now = context.sys->clock();
        DEBUG(logger, "UPDATE {}", abbrev_realtime(now));

        std::vector<DisplayScreen> display_screens;
        for (auto const& [key, screen] : script.screens) {
            auto *output = &outputs[key];
            if (output->player && matches_mode(screen, output->mode)) {
                DEBUG(logger, "  [{}] {}", output->name, debug(output->mode));
            } else {
                if (display_screens.empty())
                    display_screens = context.driver->scan_screens();

                std::string display_name;
                uint32_t display_id = 0;
                DisplayMode display_mode = {};
                for (auto const& display : display_screens) {
                    if (
                        display.connector == key ||
                        (display.display_detected && key == "*")
                    ) {
                        display_name = display.connector;
                        display_id = display.id;
                        if (matches_mode(screen, display.active_mode)) {
                            display_mode = display.active_mode;
                        } else {
                            for (auto const& mode : display.modes) {
                                if (matches_mode(screen, mode)) {
                                    display_mode = mode;
                                    break;
                                }
                            }
                        }
                        break;
                    }
                }

                if (display_id == 0) {
                    logger->error("Connector not found: \"{}\"", key);
                    continue;
                }

                if (!display_mode.nominal_hz) {
                    logger->error(
                        "Mode not found: {} {}x{} {}Hz", display_name,
                        screen.display_mode.x, screen.display_mode.y,
                        screen.display_hz
                    );
                    continue;
                }

                DEBUG(
                    logger, "  [{}] (new!) {}",
                    display_name, debug(display_mode)
                );
                output->name = display_name;
                output->mode = display_mode;
                output->player.reset();
                output->player = context.player_f(display_id, display_mode);
            }

            ASSERT(output->mode.actual_hz() > 0);
            double const script_hz = screen.update_hz;
            double const hz = script_hz ? script_hz : output->mode.actual_hz();
            double const next_output_time = std::ceil(now * hz) / hz;

            FramePlayer::Timeline timeline;
            for (size_t li = 0; li < screen.layers.size(); ++li) {
                auto const& layer = screen.layers[li];
                auto* input = &inputs[layer.media.file];
                DEBUG(logger, "    \"{}\"", layer.media.file);

                Interval const buffer{now, now + layer.media.buffer};
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
                    auto const get = [t](BezierSpline const& bez, double def) {
                        return bez.value(t).value_or(def);
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
            output->active = true;
        }

        for (const auto& standby : script.standbys) {
            auto* input = &inputs[standby.file];
            TRACE(logger, "  standby \"{}\"", standby.file);

            Interval const buffer{now, now + standby.buffer};
            auto const buffer_range = standby.play.range(buffer);
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
                    TRACE(logger, "  dormant \"{}\"", input_it->first);
                }
                input_it = inputs.erase(input_it);
            } else {
                if (input->loader) {
                    TRACE(logger, "  \"{}\"", input_it->first);
                } else {
                    DEBUG(logger, "  opening \"{}\"", input_it->first);
                    input->loader = context.loader_f(input_it->first);
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
            if (!output_it->second.active) {
                DEBUG(logger, "  [{}] stopping", output_it->second.name);
                output_it = outputs.erase(output_it);
            } else {
                status.screen_mode[output_it->first] = output_it->second.mode;
                output_it->second.active = false;
                ++output_it;
            }
        }

        TRACE(logger, "  update done");
        return status;
    }

    void init(ScriptContext cx) {
        CHECK_ARG(cx.driver, "No driver for ScriptRunner");
        context = std::move(cx);

        if (!context.sys)
            context.sys = global_system();

        if (!context.loader_f) {
            context.loader_f = [this](std::string const& file) {
                return start_frame_loader(context.driver, file, context.sys);
            };
        }

        if (!context.player_f) {
            context.player_f = [this](uint32_t id, DisplayMode const& m) {
                return start_frame_player(context.driver, id, m, context.sys);
            };
        }
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
        bool active = false;
    };

    std::shared_ptr<log::logger> const logger = runner_logger();
    ScriptContext context = {};

    std::map<std::string, Input> inputs;
    std::map<std::string, Output> outputs;
};

}  // anonymous namespace

std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext context) {
    auto runner = std::make_unique<ScriptRunnerDef>();
    runner->init(std::move(context));
    return runner;
}

}  // namespace pivid
