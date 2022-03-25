#include "script_runner.h"

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
        double const raw_t = system_t.time_since_epoch().count();

        std::vector<DisplayScreen> display_screens;
        std::map<std::string, Output> new_outputs;
        std::map<std::string, IntervalSet<Seconds>> requests;
        for (auto const& [connector, screen] : script.screens) {
            auto *output = &new_outputs[connector];

            auto output_it = outputs.find(connector);
            if (output_it != outputs.end())
                *output = std::move(output_it->second);

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
                output->refresh_time = Seconds(1.0) / screen_mode.actual_hz();
                output->start_time = steady_t;
                output->player.reset();
                output->player = player_f(screen_id, screen_mode);
            }

            for (const auto& layer : screen.layers) {
                auto* request = &requests[layer.media.file];
                (void) request;
            }
        }

        (void) steady_t;
        (void) raw_t;
        (void) signal;
        outputs = std::move(new_outputs);
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
    struct Output {
        std::string mode;
        Seconds refresh_time = {};
        SteadyTime start_time = {};
        std::unique_ptr<FramePlayer> player;
    };

    std::shared_ptr<log::logger> const logger = runner_logger();
    std::shared_ptr<DisplayDriver> driver;
    std::shared_ptr<UnixSystem> sys;
    std::function<std::unique_ptr<FrameLoader>(std::string const&)> loader_f;
    std::function<std::unique_ptr<FramePlayer>(uint32_t, DisplayMode)> player_f;

    std::map<std::string, std::unique_ptr<FrameLoader>> loaders;
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
