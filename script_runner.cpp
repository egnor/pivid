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
    ) {
        auto const steady_t = sys->steady_time();
        auto const system_t = sys->system_time();

        std::vector<DisplayScreen> display_screens;
        for (auto const& [connector, screen] : script.screens) {
            auto *output = &outputs[connector];
            if (!output->player || screen.mode != output->mode) {
                if (display_screens.empty())
                    display_screens = driver->scan_screens();

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
                output->refresh_period = Seconds(1.0) / screen_mode.actual_hz();
                output->next_refresh = steady_t;
                output->player.reset();
                output->player = player_f(screen_id, screen_mode);
            }

            while (output->next_refresh <= steady_t)
                output->next_refresh += output->refresh_period;

            std::map<SteadyTime, std::vector<DisplayLayer>> timeline;
            for (size_t li = 0; li < screen.layers.size(); ++li) {
                auto const& layer = screen.layers[li];
                auto* input = &inputs[layer.media.file];
                double const raw_sys_t = system_t.time_since_epoch().count();
                Interval<double> buf{raw_sys_t, raw_sys_t + layer.media.buffer};
                for (auto const& r : bezier_range_over(layer.media.play, buf))
                    input->request.insert({Seconds(r.begin), Seconds(r.end)});

                if (!input->loader) continue;
                if (!input->content) input->content = input->loader->content();

                for (
                    auto output_t = output->next_refresh;
                    output_t < steady_t + Seconds(layer.media.buffer);
                    output_t += output->refresh_period
                ) {
                    auto const out_sys_t = system_t + (output_t - steady_t);
                    auto const out_raw_t = out_sys_t.time_since_epoch().count();
                    auto const get = [&](BezierSpline const& bez, double def) {
                        return bezier_value_at(bez, out_raw_t).value_or(def);
                    };

                    auto const media_raw_t = get(layer.media.play, -1);
                    if (media_raw_t < 0) continue;
                    auto media_t = Seconds(media_raw_t);

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
                    timeline[output_t].push_back(std::move(display));
                }
            }

            output->player->set_timeline(timeline, signal);
        }

        for (const auto& standby : script.standbys) {
            auto* input = &inputs[standby.file];
            double const raw_sys_t = system_t.time_since_epoch().count();
            Interval<double> buf{raw_sys_t, raw_sys_t + standby.buffer};
            for (auto const& r : bezier_range_over(standby.play, buf))
                input->request.insert({Seconds(r.begin), Seconds(r.end)});
        }

        auto input_it = inputs.begin();
        while (input_it != inputs.end()) {
            if (input_it->second.request.empty()) {
                input_it = inputs.erase(input_it);
            } else {
                auto *input = &input_it->second;
                if (!input->loader) input->loader = loader_f(input_it->first);
                input->loader->set_request(input->request, signal);
                input->request = {};
                input->content = {};
                ++input_it;
            }
        }

        auto output_it = outputs.begin();
        while (output_it != outputs.end()) {
            if (output_it->second.next_refresh < steady_t) {
                output_it = outputs.erase(output_it);
            } else {
                ++output_it;
            }
        }
    }

    void init(
        std::shared_ptr<DisplayDriver> driver,
        std::shared_ptr<UnixSystem> sys,
        std::function<std::unique_ptr<FrameLoader>(std::string const&)> lf,
        std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> pf
    ) {
        this->driver = driver;
        this->sys = sys;
        this->loader_f = std::move(lf);
        this->player_f = std::move(pf);
    }

  private:
    struct Input {
        IntervalSet<Seconds> request;
        std::unique_ptr<FrameLoader> loader;
        std::optional<FrameLoader::Content> content;
    };

    struct Output {
        std::string mode;
        XY<int> size;
        Seconds refresh_period = {};
        SteadyTime next_refresh = {};
        std::unique_ptr<FramePlayer> player;
    };

    std::shared_ptr<log::logger> const logger = runner_logger();
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> loader_f;
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> player_f;

    std::map<std::string, Input> inputs;
    std::map<std::string, Output> outputs;
};

}  // anonymous namespace

std::unique_ptr<ScriptRunner> make_script_runner(
    std::shared_ptr<DisplayDriver> driver,
    std::shared_ptr<UnixSystem> sys,
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> loader_f,
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> player_f
) {
    auto runner = std::make_unique<ScriptRunnerDef>();
    runner->init(
        std::move(driver), std::move(sys),
        std::move(loader_f), std::move(player_f)
    );
    return runner;
}

}  // namespace pivid
