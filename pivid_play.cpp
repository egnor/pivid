// Simple command line tool to exercise video decoding and playback.

#include <cmath>
#include <fstream>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "display_output.h"
#include "logging_policy.h"
#include "script_data.h"
#include "script_runner.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& play_logger() {
    static const auto logger = make_logger("pivid_play");
    return logger;
}

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    fmt::print("=== Video drivers ===\n");
    std::optional<DisplayDriverListing> found;
    for (auto const& d : list_display_drivers(global_system())) {
        auto const text = debug(d);
        if (!found && text.find(dev_arg) != std::string::npos)
            found = d;
        fmt::print("{} {}\n", (found == d) ? "=>" : "  ", debug(d));
    }
    fmt::print("\n");

    CHECK_RUNTIME(found, "No DRM device matching \"{}\"", dev_arg);
    return open_display_driver(global_system(), found->dev_file);
}

void set_kernel_debug(bool enable) {
    auto const debug_file = "/sys/module/drm/parameters/debug";
    auto const debug_stat = global_system()->stat(debug_file).ex(debug_file);
    if ((debug_stat.st_mode & 022) == 0 && debug_stat.st_uid == 0) {
        if (!enable) return;  // No permissions, assume disabled
        std::vector<std::string> argv = {"sudo", "chmod", "go+rw", debug_file};
        fmt::print("!!! Running:");
        for (auto const& arg : argv) fmt::print(" {}", arg);
        fmt::print("\n");
        fflush(stdout);
        auto const pid = global_system()->spawn(argv[0], argv).ex(argv[0]);
        auto const ex = global_system()->wait(P_PID, pid, WEXITED).ex(argv[0]);
        CHECK_RUNTIME(!ex.si_status, "Kernel debug chmod error");
    }

    auto const fd = global_system()->open(debug_file, O_WRONLY).ex(debug_file);
    auto const val = fmt::format("0x{:x}", enable ? 0x3DF : 0);
    fmt::print("Kernel debug: Writing {} to {}\n\n", val, debug_file);
    fd->write(val.data(), val.size()).ex(debug_file);
}

Script make_script(
    std::string const& media_arg,
    std::string const& screen_arg,
    XY<int> mode_size,
    double seek_arg
) {
    Script script;
    auto *screen = &script.screens.try_emplace(screen_arg).first->second;
    screen->display_mode = mode_size;

    if (media_arg.empty()) {
        play_logger()->warn("No media to play");
    } else {
        ScriptLayer* layer = &screen->layers.emplace_back();
        layer->media = media_arg;
        layer->play.segments.push_back(
            linear_segment({0, 0 + 1e12}, {seek_arg, seek_arg + 1e12})
        );
    }

    script.zero_time = global_system()->clock();
    play_logger()->info("Start: {}", format_realtime(script.zero_time));
    return script;
}

Script load_script(std::string const& script_file) {
    auto const logger = play_logger();
    auto const sys = global_system();

    std::ifstream ifs;
    ifs.exceptions(~std::ifstream::goodbit);
    ifs.open(script_file, std::ios::binary);
    std::string const text(
        (std::istreambuf_iterator<char>(ifs)),
        (std::istreambuf_iterator<char>())
    );

    double const default_zero_time = sys->clock();
    play_logger()->info("Start: {}", format_realtime(default_zero_time));
    return parse_script(text, default_zero_time);
}

void run_script(ScriptContext const& context, Script const& script) {
    auto const logger = play_logger();
    auto const sys = global_system();
    auto const waiter = sys->make_flag(CLOCK_MONOTONIC);

    ASSERT(script.main_loop_hz > 0);
    double const period = 1.0 / script.main_loop_hz;
    double loop_mono = 0.0;

    auto const runner = make_script_runner(context);
    for (;;) {
        loop_mono = std::max(sys->clock(CLOCK_MONOTONIC), loop_mono + period);
        waiter->sleep_until(loop_mono);
        runner->update(script);

        bool done = true;
        const double now_t0 = sys->clock() - script.zero_time;
        for (auto const& [conn, screen] : script.screens) {
            for (auto const& layer : screen.layers) {
                auto const range = layer.play.range({now_t0, now_t0 + 1e12});
                auto const& info = runner->file_info(layer.media);
                if (info.duration) {
                    TRACE(
                        logger, "{:.3f} / {:.3f}s: {}",
                        range.bounds().begin, *info.duration, layer.media
                    );
                    if (range.bounds().begin < *info.duration) done = false;
                }
            }
        }

        if (done) {
            logger->info("All media done playing");
            break;
        }
    }
}

}  // namespace

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg;
    std::string screen_arg = "*";
    std::string log_arg;
    std::string media_arg;
    std::string script_arg;
    XY<int> mode_arg = {0, 0};
    double seek_arg = -0.2;
    bool debug_kernel = false;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver description substring");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode_x", mode_arg.x, "Video pixels per line");
    app.add_option("--mode_y", mode_arg.y, "Video scan lines");
    app.add_option("--screen", screen_arg, "Video output connector");
    app.add_option("--seek", seek_arg, "Seconds into media to start");
    app.add_flag("--debug_kernel", debug_kernel, "Enable kernel DRM debugging");

    auto input = app.add_option_group("Input")->require_option(0, 1);
    input->add_option("--media", media_arg, "Media file to play");
    input->add_option("--script", script_arg, "Script file to play");

    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    set_kernel_debug(debug_kernel);
    auto const logger = play_logger();

    try {
        ScriptContext context = {};
        context.driver = find_driver(dev_arg);

        Script script;
        if (!script_arg.empty()) {
            logger->info("Script: {}", script_arg);
            context.file_base = script_arg;
            script = load_script(script_arg);
        } else {
            context.file_base = global_system()->realpath(".").ex("getcwd");
            script = make_script(media_arg, screen_arg, mode_arg, seek_arg);
        }

        run_script(context, script);
    } catch (std::exception const& e) {
        logger->critical("{}", e.what());
        return 1;
    }

    fmt::print("Done!\n\n");
    return 0;
}

}  // namespace pivid
