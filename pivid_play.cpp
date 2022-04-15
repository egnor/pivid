// Simple command line tool to exercise video decoding and playback.

#include <cmath>
#include <fstream>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

extern "C" {
#include <libavutil/log.h>  // For --debug_libav
}

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
        const double start = global_system()->clock();
        play_logger()->info("Start: {}", abbrev_realtime(start));

        ScriptLayer* layer = &screen->layers.emplace_back();
        layer->media.file = media_arg;
        layer->media.play.segments.push_back(
            linear_segment({start, start + 1e12}, {seek_arg, seek_arg + 1e12})
        );
    }

    return script;
}

void fix_time(double start, BezierSpline* fix) {
    // Assume small values are relative.
    for (auto& seg : fix->segments) {
        if (seg.t.begin < 1e7) seg.t.begin += start;
        if (seg.t.end < 1e7) seg.t.end += start;
    }
}

void fix_time(double start, XY<BezierSpline>* fix) {
    fix_time(start, &fix->x);
    fix_time(start, &fix->y);
}

Script load_script(std::string const& script_file) {
    auto const logger = play_logger();
    auto const sys = global_system();
    logger->info("Loading script: {}", script_file);

    std::ifstream ifs;
    ifs.exceptions(~std::ifstream::goodbit);
    ifs.open(script_file, std::ios::binary);
    std::string const text(
        (std::istreambuf_iterator<char>(ifs)),
        (std::istreambuf_iterator<char>())
    );

    nlohmann::json json;
    try {
        json = nlohmann::json::parse(text);
    } catch (nlohmann::json::parse_error const& je) {
        throw_with_nested(std::invalid_argument(je.what()));
    }

    auto const start = global_system()->clock();
    logger->info("Start: {}", abbrev_realtime(start));

    auto script = json.get<Script>();
    for (auto& [conn, screen] : script.screens) {
        for (auto& layer : screen.layers) {
            fix_time(start, &layer.media.play);
            fix_time(start, &layer.from_xy);
            fix_time(start, &layer.from_size);
            fix_time(start, &layer.to_xy);
            fix_time(start, &layer.to_size);
            fix_time(start, &layer.opacity);
        }
    }
    for (auto& standby : script.standbys) {
        fix_time(start, &standby.play);
    }
    return script;
}

void run_script(ScriptContext const& context, Script const& script) {
    auto const logger = play_logger();
    auto const sys = global_system();
    auto const waiter = sys->make_flag(CLOCK_MONOTONIC);

    ASSERT(script.main_loop_hz > 0);
    double const period = 1.0 / script.main_loop_hz;
    double loop_time = 0.0;

    auto const runner = make_script_runner(context);
    for (;;) {
        loop_time = std::max(sys->clock(CLOCK_MONOTONIC), loop_time + period);
        waiter->sleep_until(loop_time);
        runner->update(script);

        bool done = true;
        const double now = sys->clock();
        for (auto const& [conn, screen] : script.screens) {
            for (auto const& layer : screen.layers) {
                auto const range = layer.media.play.range({now, now + 1e12});
                auto const& info = runner->file_info(layer.media.file);
                if (info.duration && range.bounds().begin < *info.duration) {
                    TRACE(
                        logger, "{:.3f} / {:.3f}s: {}",
                        range.bounds().begin, *info.duration, layer.media.file
                    );
                    done = false;
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
    bool debug_libav = false;
    bool debug_kernel = false;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver description substring");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode_x", mode_arg.x, "Video pixels per line");
    app.add_option("--mode_y", mode_arg.y, "Video scan lines");
    app.add_option("--screen", screen_arg, "Video output connector");
    app.add_option("--seek", seek_arg, "Seconds into media to start");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    app.add_flag("--debug_kernel", debug_kernel, "Enable kernel DRM debugging");

    auto input = app.add_option_group("Input")->require_option(0, 1);
    input->add_option("--media", media_arg, "Media file to play");
    input->add_option("--script", script_arg, "Script file to play");

    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);
    set_kernel_debug(debug_kernel);

    try {
        ScriptContext context = {};
        context.driver = find_driver(dev_arg);

        if (!script_arg.empty()) {
            context.file_base = script_arg;
            run_script(context, load_script(script_arg));
        } else {
            context.file_base = global_system()->realpath(".").ex("getcwd");
            auto scr = make_script(media_arg, screen_arg, mode_arg, seek_arg);
            run_script(context, scr);
        }
    } catch (std::exception const& e) {
        play_logger()->critical("{}", e.what());
        return 1;
    }

    fmt::print("Done!\n\n");
    return 0;
}

}  // namespace pivid
