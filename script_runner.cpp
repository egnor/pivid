#include "script_runner.h"

#include <optional>

#include "display_output.h"
#include "frame_loader.h"
#include "frame_player.h"
#include "logging_policy.h"
#include "thread_signal.h"

namespace pivid {

namespace {

auto const& runner_logger() {
    static const auto logger = make_logger("runner");
    return logger;
}

class ScriptRunnerDef : public ScriptRunner {
  public:
    virtual void update(
        Script const& script,
        std::shared_ptr<ThreadSignal> signal
    ) final {
        DEBUG(logger, "UPDATE");
        auto const now = context.sys->system_time();

        std::vector<DisplayScreen> display_screens;
        for (auto const& [connector, screen] : script.screens) {
            auto *output = &outputs[connector];
            if (output->player && screen.mode == output->mode) {
                TRACE(logger, "> use {} player: {}", connector, screen.mode);
            } else {
                DEBUG(
                    logger, "> {} {} player: {}",
                    output->player ? "reset" : "start", connector, screen.mode
                );

                if (display_screens.empty())
                    display_screens = context.driver->scan_screens();

                uint32_t screen_id = 0;
                DisplayMode screen_mode = {};
                for (auto const& display : display_screens) {
                    if (display.connector == connector) {
                        for (auto const& mode : display.modes) {
                            if (mode.name == screen.mode) {
                                screen_mode = mode;
                                break;
                            }
                        }

                        screen_id = display.id;
                        break;
                    }
                }

                if (screen_id == 0) {
                    logger->error("Connector not found: \"{}\"", connector);
                    continue;
                }

                if (screen_mode.nominal_hz == 0) {
                    logger->error("Mode not found: \"{}\"", screen.mode);
                    continue;
                }

                output->mode = screen.mode;
                output->size = screen_mode.size;
                output->refresh_period = 1.0 / screen_mode.actual_hz();
                output->player.reset();
                output->player = context.player_f(screen_id, screen_mode);
            }

            double next_output_time = 
                std::ceil(now / output->refresh_period) *
                output->refresh_period;

            FramePlayer::Timeline timeline;
            for (size_t li = 0; li < screen.layers.size(); ++li) {
                auto const& layer = screen.layers[li];
                auto* input = &inputs[layer.media.file];
                TRACE(logger, ">> layer \"{}\"", layer.media.file);

                Interval buf{now, now + layer.media.buffer};
                for (auto const& r : layer.media.play.range(buf)) {
                    Interval range{r.begin, r.end};
                    TRACE(logger, ">>> want {}", debug(range));
                    input->request.insert(range);
                }

                if (!input->content) {
                    if (!input->loader) continue;
                    input->content = input->loader->content();
                }
                TRACE(logger, ">>> have {}", debug(input->content->have));

                int frame_count = 0, unique_count = 0;
                std::shared_ptr<LoadedImage> last_image = {};

                double output_time;
                for (
                    output_time = next_output_time;
                    output_time < now + layer.media.buffer;
                    output_time += output->refresh_period
                ) {
                    auto const get = [&](BezierSpline const& bez, double def) {
                        return bez.value(output_time).value_or(def);
                    };

                    auto const media_t = get(layer.media.play, -1);
                    if (media_t < 0) continue;

                    auto frame_it = input->content->frames.upper_bound(media_t);
                    if (frame_it != input->content->frames.begin()) continue;
                    --frame_it;

                    auto const image_size = frame_it->second->size();
                    DisplayLayer display = {};
                    display.image = frame_it->second;
                    display.from_xy.x = get(layer.from_xy.x, 0);
                    display.from_xy.y = get(layer.from_xy.y, 0);
                    display.from_size.x = get(layer.from_size.x, image_size.x);
                    display.from_size.y = get(layer.from_size.y, image_size.y);
                    display.to_xy.x = get(layer.to_xy.x, 0);
                    display.to_xy.y = get(layer.to_xy.y, 0);
                    display.to_size.x = get(layer.to_xy.x, output->size.x);
                    display.to_size.y = get(layer.to_xy.y, output->size.y);
                    display.opacity = get(layer.opacity, 1);
                    timeline[output_time].push_back(std::move(display));

                    ++frame_count;
                    if (display.image != last_image) {
                        ++unique_count;
                        last_image = display.image;
                    }
                }

                TRACE(
                    logger, ">>> add {}fr ({}im) {}~{}s",
                    frame_count, unique_count,
                    abbrev_time(next_output_time), abbrev_time(output_time)
                );
            }

            output->player->set_timeline(std::move(timeline), signal);
            output->active = true;
        }

        for (const auto& standby : script.standbys) {
            auto* input = &inputs[standby.file];
            TRACE(logger, ">> standby \"{}\"", standby.file);

            Interval buf{now, now + standby.buffer};
            for (auto const& r : standby.play.range(buf)) {
                Interval range{r.begin, r.end};
                TRACE(logger, ">>> want {}", debug(range));
                input->request.insert(range);
            }
        }

        auto input_it = inputs.begin();
        while (input_it != inputs.end()) {
            auto *input = &input_it->second;
            if (input->request.empty()) {
                DEBUG(logger, "> drop \"{}\" loader", input_it->first);
                input_it = inputs.erase(input_it);
            } else {
                if (input->loader) {
                    DEBUG(logger, "> use \"{}\" loader", input_it->first);
                } else {
                    DEBUG(logger, "> start \"{}\" loader", input_it->first);
                    input->loader = context.loader_f(input_it->first);
                }

                DEBUG(logger, ">> request {}", debug(input->request));
                input->loader->set_request(input->request, signal);
                input->request = {};
                input->content = {};
                ++input_it;
            }
        }

        auto output_it = outputs.begin();
        while (output_it != outputs.end()) {
            if (!output_it->second.active) {
                DEBUG(logger, "> drop {} player", output_it->first);
                output_it = outputs.erase(output_it);
            } else {
                output_it->second.active = false;
                ++output_it;
            }
        }
    }

    void init(ScriptContext cx) {
        CHECK_ARG(cx.driver, "No driver for ScriptRunner");
        context = std::move(cx);

        if (!context.sys)
            context.sys = global_system();

        if (!context.loader_f) {
            context.loader_f = [this](std::string const& file) {
                return start_frame_loader(context.driver, file);
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
        std::string mode;
        XY<int> size;
        double refresh_period = {};
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
