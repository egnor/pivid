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

std::map<ScriptMode, DisplayMode> make_mode_map() {
    std::map<ScriptMode, DisplayMode> modes;
    for (auto const& mode : vesa_dmt_modes) {
        // Prefer modes with *higher* pixel clock, to select against
        // Reduced Blanking modes. TODO: Maybe RB is good?
        auto* slot = &modes[{mode.size, mode.nominal_hz}];
        if (!slot->nominal_hz || slot->pixel_khz < mode.pixel_khz)
            *slot = mode;
    }

    // Prefer CTA modes (we use HDMI after all), so overlay them on top.
    for (auto const& mode : cta_861_modes) {
        auto* slot = &modes[{mode.size, mode.nominal_hz}];
        *slot = mode;
        slot->aspect = {};  // Ignore various aspect ratio variants 
    }

    return modes;
}

std::map<ScriptMode, DisplayMode> const& get_mode_map() {
    static auto const map = make_mode_map();
    return map;
}

bool matches(ScriptMode const& sm, DisplayMode const& dm) {
    return sm.size == dm.size && sm.hz == dm.nominal_hz;
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

            if (output->player && matches(script_screen.mode, output->mode)) {
                DEBUG(logger, "  [{}] {}", connector, debug(output->mode));
            } else {
                if (display_screens.empty())
                    display_screens = cx.driver->scan_screens();

                uint32_t display_id = 0;
                DisplayMode mode = {};
                for (auto const& display : display_screens) {
                    if (display.connector != connector) continue;
                    display_id = display.id;

                    // If screen-off is requested, use the zero-init mode.
                    if (!script_screen.mode.hz) break;

                    // If the active mode matches the spec, use it.
                    if (matches(script_screen.mode, display.active_mode)) {
                        mode = display.active_mode;
                        break;
                    }

                    // Look for a canned mode matching the spec.
                    auto const& mode_map = get_mode_map();
                    auto const it = mode_map.find(script_screen.mode);
                    if (it != mode_map.end()) {
                        mode = it->second;
                        break;
                    }

                    // Finally, try to synthesize a CVT mode for spec.
                    auto const cvt = vesa_cvt_mode(
                        script_screen.mode.size,
                        script_screen.mode.hz
                    );
                    if (cvt) mode = *cvt;
                    break;
                }

                if (display_id == 0) {
                    logger->error("Connector not found: \"{}\"", connector);
                    continue;
                }

                if (!matches(script_screen.mode, mode)) {
                    logger->error(
                        "Mode not found: {}x{} {}Hz",
                        script_screen.mode.size.x,
                        script_screen.mode.size.y,
                        script_screen.mode.hz
                    );
                    continue;
                }

                DEBUG(logger, "  [{}] + {}", connector, debug(mode));
                if (!output->player)
                    output->player = cx.player_f(display_id);
                output->mode = mode;
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

            // Create empty timeline elements at each frame time
            FramePlayer::Timeline timeline;
            for (double t = begin_t; t < end_t; t += 1.0 / hz) {
                auto* frame = &timeline[t];
                frame->mode = output->mode;
                frame->layers.reserve(script_screen.layers.size());
            }

            for (size_t li = 0; li < script_screen.layers.size(); ++li) {
                auto const& script_layer = script_screen.layers[li];
                auto const& file = find_file(lock, script_layer.media);
                auto* input = &input_media[file];
                DEBUG(logger, "    \"{}\"", short_filename(file));

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

                for (auto& [t, t_frame] : timeline) {
                    auto const media_t = script_layer.play.value(t - t0);
                    if (!media_t) {
                        TRACE(logger, "      {:+.3f}s inactive", t - now);
                        continue;
                    }

                    if (*media_t < 0) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s before start",
                            t - now, *media_t
                        );
                        continue;
                    }

                    if (input->frames->eof && *media_t >= *input->frames->eof) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s after EOF",
                            t - now, *media_t
                        );
                        continue;
                    }

                    if (!input->frames->coverage.contains(*media_t)) {
                        TRACE(
                            logger, "      {:+.3f}s m{:.3f}s not loaded!",
                            t - now, *media_t
                        );

                        t_frame.warnings.push_back(fmt::format(
                            "underrun @{:.3f}s \"{}\"", *media_t, file
                        ));
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
                    auto const size = fit->second->content().size;
                    auto* layer = &t_frame.layers.emplace_back();
                    layer->from_xy.x = bez(script_layer.from_xy.x, 0);
                    layer->from_xy.y = bez(script_layer.from_xy.y, 0);
                    layer->from_size.x = bez(script_layer.from_size.x, size.x);
                    layer->from_size.y = bez(script_layer.from_size.y, size.y);
                    layer->to_xy.x = bez(script_layer.to_xy.x, 0);
                    layer->to_xy.y = bez(script_layer.to_xy.y, 0);
                    layer->to_size.x = bez(script_layer.to_size.x, size.x);
                    layer->to_size.y = bez(script_layer.to_size.y, size.y);
                    layer->opacity = bez(script_layer.opacity, 1);
                    TRACE(
                        logger, "      {:+.3f}s m{:.3f} f{:.3f} {}",
                        t - now, *media_t, frame_t, debug(*layer)
                    );

                    layer->image = fit->second;  // Not in TRACE above
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
                DEBUG(logger, "  [{}] unspecified, blanking", conn);
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
            cx.player_f = [this](uint32_t id) {
                return start_frame_player(cx.driver, id, cx.sys);
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
