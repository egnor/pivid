#include "script_runner.h"

#include <mutex>
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
    virtual void update(Script const& script) final {
        std::unique_lock lock{mutex};
        auto const now = cx.sys->clock();
        auto const t0 = script.zero_time;
        DEBUG(logger, "UPDATE {} (t0+{:.3f}s)", abbrev_realtime(now), now - t0);
        for (const auto& [media, script_media] : script.media) {
            auto const& file = find_file(lock, media);
            auto* input = &input_media[file];
            TRACE(logger, "  media \"{}\"", file);

            input->req.decoder_idle_time = script_media.decoder_idle_time;
            input->req.seek_scan_time = script_media.seek_scan_time;
            TRACE(
                logger, "    idle={:.3f}s scan={:.3f}s",
                input->req.decoder_idle_time,
                input->req.seek_scan_time
            );

            for (auto const& preload : script_media.preload) {
                auto const begin = preload.begin.value(now - t0);
                auto const end = preload.end.value(now - t0);
                if (begin && end) {
                    Interval want{*begin, *end};
                    TRACE(logger, "    preload {}", debug(want));
                    input->req.wanted.insert(want);
                } else {
                    TRACE(logger, "    preload inactive");
                }
            }
        }

        std::vector<DisplayScreen> display_screens;
        for (auto const& [connector, script_screen] : script.screens) {
            auto *output = &output_screens[connector];
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
                    if (!matches_display(connector, display)) continue;
                    display_name = display.connector;
                    display_id = display.id;

                    for (auto const& mode : display.modes) {
                        if (!matches_mode(script_screen, mode)) continue;
                        display_mode = (
                            mode.size == display.active_mode.size &&
                            mode.nominal_hz == display.active_mode.nominal_hz
                        ) ? display.active_mode : mode;
                        break;
                    }
                    break;
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
                output->player->set_timeline({});
                continue;
            }

            ASSERT(output->mode.actual_hz() > 0);
            double const script_hz = script_screen.update_hz;
            double const hz = script_hz ? script_hz : output->mode.actual_hz();
            double const begin_t = std::ceil(now * hz) / hz;
            double const end_t = now + script.main_buffer_time;

            FramePlayer::Timeline timeline;
            for (double t = begin_t; t < end_t; t += 1.0 / hz)
                timeline[t];  // Create empty timeline element

            for (size_t li = 0; li < script_screen.layers.size(); ++li) {
                auto const& script_layer = script_screen.layers[li];
                auto const& file = find_file(lock, script_layer.media);
                auto* input = &input_media[file];
                DEBUG(logger, "    \"{}\"", file);

                auto const rt = now - t0;
                Interval const buffer_t{rt, rt + script_layer.buffer};
                IntervalSet const want = script_layer.play.range(buffer_t);
                TRACE(logger, "      want {}", debug(want));
                input->req.wanted.insert(want);

                if (!input->frames) {
                    if (!input->loader) continue;
                    input->frames = input->loader->frames();
                }
                TRACE(logger, "      have {}", debug(input->frames->coverage));

                for (auto& [t, t_layers] : timeline) {
                    auto const media_t = script_layer.play.value(t - t0);
                    if (!media_t) {
                        TRACE(logger, "      {:+.3f}s inactive", t - now);
                        continue;
                    } else if (*media_t < 0) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s before start!",
                            t - now, *media_t
                        );
                        continue;
                    }

                    if (!input->frames->coverage.contains(*media_t)) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s not loaded!",
                            t - now, *media_t
                        );
                        continue;
                    }

                    auto fit = input->frames->frames.upper_bound(*media_t);
                    if (fit == input->frames->frames.begin()) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s empty media",
                            t - now, *media_t
                        );
                        continue;
                    }

                    auto const bez = [&](BezierSpline const& z, double def) {
                        return z.value(t - t0).value_or(def);
                    };

                    --fit;
                    auto const frame_t = fit->first;
                    auto const size = fit->second->size();
                    auto* out = &t_layers.emplace_back();
                    out->from_xy.x = bez(script_layer.from_xy.x, 0);
                    out->from_xy.y = bez(script_layer.from_xy.y, 0);
                    out->from_size.x = bez(script_layer.from_size.x, size.x);
                    out->from_size.y = bez(script_layer.from_size.y, size.y);
                    out->to_xy.x = bez(script_layer.to_xy.x, 0);
                    out->to_xy.y = bez(script_layer.to_xy.y, 0);
                    out->to_size.x = bez(script_layer.to_size.x, size.x);
                    out->to_size.y = bez(script_layer.to_size.y, size.y);
                    out->opacity = bez(script_layer.opacity, 1);
                    TRACE(
                        logger, "      {:+.3f}s m{:.3f} f{:.3f} {}",
                        t - now, *media_t, frame_t, debug(*out)
                    );

                    out->image = fit->second;  // Not in TRACE above
                }
            }

            output->player->set_timeline(std::move(timeline));
        }

        auto input_it = input_media.begin();
        while (input_it != input_media.end()) {
            auto *input = &input_it->second;
            if (input->req.wanted.empty()) {
                if (input->loader) {
                    DEBUG(logger, "  closing \"{}\"", input_it->first);
                } else {
                    TRACE(logger, "  unused \"{}\"", input_it->first);
                }
                input_it = input_media.erase(input_it);
            } else {
                if (input->loader) {
                    TRACE(logger, "  refresh \"{}\"", input_it->first);
                } else {
                    DEBUG(logger, "  opening \"{}\"", input_it->first);
                    auto loader_cx = cx.loader_cx;
                    loader_cx.filename = input_it->first;
                    input->loader = cx.loader_f(std::move(loader_cx));
                }

                TRACE(logger, "    request {}", debug(input->req.wanted));
                input->loader->set_request(std::move(input->req));
                input->req = {};
                input->frames = {};
                ++input_it;
            }
        }

        for (auto& [conn, output] : output_screens) {
            if (!output.defined) {
                DEBUG(logger, "  [{}] unspecified, blanking", output.name);
                output.player->set_timeline({});
            } else {
                output.defined = false;
            }
        }

        TRACE(logger, "  update done");
    }

    MediaFileInfo const& file_info(std::string const& spec) final {
        std::unique_lock lock{mutex};
        auto const file = find_file(lock, spec);
        auto cache_it = info_cache.find(file);
        if (cache_it != info_cache.end()) {
            TRACE(logger, "FILE INFO {}", debug(cache_it->second));
        } else {
            auto loader = input_media[file].loader;

            lock.unlock();
            if (!loader) {
                TRACE(logger, "Opening \"{}\" for info", file);
                auto loader_cx = cx.loader_cx;
                loader_cx.filename = file;
                loader = cx.loader_f(std::move(loader_cx));
            }

            auto info = loader->file_info();
            DEBUG(logger, "FILE INFO {}", debug(info));
            lock.lock();  // State may have changed!

            auto* input = &input_media[file];
            if (!input->loader) input->loader = loader;
            cache_it = info_cache.insert({file, std::move(info)}).first;
        }

        return cache_it->second;
    }

    void init(ScriptContext c) {
        cx = std::move(c);
        if (!cx.sys) cx.sys = global_system();
        CHECK_ARG(cx.driver, "No driver for ScriptRunner");

        if (cx.root_dir.empty()) cx.root_dir = "/";
        if (cx.file_base.empty()) cx.file_base = ".";
        cx.root_dir = cx.sys->realpath(cx.root_dir).ex(
            fmt::format("Root dir ({})", cx.root_dir)
        );
        cx.file_base = cx.sys->realpath(cx.file_base).ex(
            fmt::format("File base ({})", cx.file_base)
        );
        if (!S_ISDIR(cx.sys->stat(cx.file_base).ex(cx.file_base).st_mode)) {
            auto const slash = cx.file_base.rfind('/');
            if (slash >= 1) cx.file_base.resize(slash);
        }
        if (!cx.root_dir.ends_with('/')) cx.root_dir += "/";
        if (!cx.file_base.ends_with('/')) cx.file_base += "/";

        if (!cx.loader_cx.sys) cx.loader_cx.sys = cx.sys;
        if (!cx.loader_cx.driver) cx.loader_cx.driver = cx.driver;
        if (!cx.loader_cx.decoder_f)
            cx.loader_cx.decoder_f = open_media_decoder;
        if (!cx.loader_f)
            cx.loader_f = start_frame_loader;

        if (!cx.player_f) {
            cx.player_f = [this](uint32_t id, DisplayMode const& m) {
                return start_frame_player(cx.driver, id, m, cx.sys);
            };
        }
    }

  private:
    struct InputMedia {
        std::shared_ptr<FrameLoader> loader;
        std::optional<LoadedFrames> frames;
        FrameRequest req;
    };

    struct OutputScreen {
        std::string name;
        XY<int> size;
        DisplayMode mode;
        std::unique_ptr<FramePlayer> player;
        bool defined = false;
    };

    // Constant from init to ~
    std::shared_ptr<log::logger> const logger = runner_logger();
    ScriptContext cx = {};

    // Guarded by mutex
    std::mutex mutable mutex;
    std::map<std::string, InputMedia> input_media;
    std::map<std::string, OutputScreen> output_screens;
    std::map<std::string, std::string> path_cache;
    std::map<std::string, MediaFileInfo> info_cache;

    std::string const& find_file(
        std::unique_lock<std::mutex> const&, std::string const& spec
    ) {
        CHECK_ARG(!spec.empty(), "Empty filename");
        auto cache_it = path_cache.find(spec);
        if (cache_it == path_cache.end()) {
            auto const lookup = spec.starts_with('/')
                ? cx.root_dir + spec.substr(1) : cx.file_base + spec;
            auto realpath = cx.sys->realpath(lookup).ex(
                fmt::format("Media \"{}\" ({})", spec, lookup)
            );
            CHECK_ARG(
                realpath.starts_with(cx.root_dir),
                "Media \"{}\" ({}) outside root ({})",
                spec, realpath, cx.root_dir
            );
            cache_it = path_cache.insert({spec, std::move(realpath)}).first;
        }

        return cache_it->second;
    }
};

}  // anonymous namespace

std::unique_ptr<ScriptRunner> make_script_runner(ScriptContext cx) {
    auto runner = std::make_unique<ScriptRunnerDef>();
    runner->init(std::move(cx));
    return runner;
}

}  // namespace pivid
