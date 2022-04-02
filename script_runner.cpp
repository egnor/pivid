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
        (!screen.mode.x || screen.mode.x == mode.size.x) &&
        (!screen.mode.y || screen.mode.y == mode.size.y) &&
        (!screen.mode_hz || screen.mode_hz == mode.nominal_hz)
    );
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
            if (output->player && matches_mode(screen, output->mode)) {
                TRACE(logger, "> use {}: {}", connector, debug(output->mode));
            } else {
                DEBUG(
                    logger, "> {} {}: {}",
                    output->player ? "reset" : "start", connector,
                    debug(output->mode)
                );

                if (display_screens.empty())
                    display_screens = context.driver->scan_screens();

                uint32_t display_id = 0;
                DisplayMode display_mode = {};
                for (auto const& display : display_screens) {
                    if (display.connector != connector) continue;
                    for (auto const& mode : display.modes) {
                        if (!matches_mode(screen, mode)) continue;
                        display_mode = mode;
                        break;
                    }
                    display_id = display.id;
                    break;
                }

                if (display_id == 0) {
                    logger->error("Connector not found: \"{}\"", connector);
                    continue;
                }

                if (!display_mode.nominal_hz) {
                    logger->error(
                        "Mode not found: {}x{} {}Hz",
                        screen.mode.x, screen.mode.y, screen.mode_hz
                    );
                    continue;
                }

                ASSERT(display_mode.actual_hz() > 0);
                output->mode = display_mode;
                output->player.reset();
                output->player = context.player_f(display_id, display_mode);
            }

            double const hz = output->mode.actual_hz();
            double const next_output_time = std::ceil(now * hz) / hz;

            FramePlayer::Timeline timeline;
            for (size_t li = 0; li < screen.layers.size(); ++li) {
                auto const& layer = screen.layers[li];
                auto* input = &inputs[layer.media.file];
                TRACE(logger, ">> layer \"{}\"", layer.media.file);

                Interval const buffer{now, now + layer.media.buffer};
                auto const range = layer.media.play.range(buffer);
                TRACE(logger, ">>> want {}", debug(range));
                input->request.insert(range);

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
                    output_time += 1.0 / hz
                ) {
                    auto const get = [&](BezierSpline const& bez, double def) {
                        return bez.value(output_time).value_or(def);
                    };

                    auto const media_t = get(layer.media.play, -1);
                    if (!input->content->have.contains(media_t)) continue;
                    auto frame_it = input->content->frames.upper_bound(media_t);
                    if (frame_it == input->content->frames.begin()) continue;
                    --frame_it;

                    auto const screen_size = output->mode.size;
                    auto const image_size = frame_it->second->size();
                    auto* display = &timeline[output_time].emplace_back();
                    display->image = frame_it->second;
                    display->from_xy.x = get(layer.from_xy.x, 0);
                    display->from_xy.y = get(layer.from_xy.y, 0);
                    display->from_size.x = get(layer.from_size.x, image_size.x);
                    display->from_size.y = get(layer.from_size.y, image_size.y);
                    display->to_xy.x = get(layer.to_xy.x, 0);
                    display->to_xy.y = get(layer.to_xy.y, 0);
                    display->to_size.x = get(layer.to_xy.x, screen_size.x);
                    display->to_size.y = get(layer.to_xy.y, screen_size.y);
                    display->opacity = get(layer.opacity, 1);

                    ++frame_count;
                    if (display->image != last_image) {
                        ++unique_count;
                        last_image = display->image;
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

            auto const range = standby.play.range({now, now + standby.buffer});
            TRACE(logger, ">>> want {}", debug(range));
            input->request.insert(range);
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
                DEBUG(logger, "> drop {}", output_it->first);
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
