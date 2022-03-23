// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <fstream>
#include <numeric>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>
#include <nlohmann/json.hpp>

extern "C" {
#include <libavutil/log.h>
}

#include "display_output.h"
#include "frame_loader.h"
#include "frame_player.h"
#include "logging_policy.h"
#include "media_decoder.h"
#include "script_data.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& main_logger() {
    static const auto logger = make_logger("main");
    return logger;
}

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    if (dev_arg == "none" || dev_arg == "/dev/null") return {};

    fmt::print("=== Video drivers ===\n");
    std::optional<DisplayDriverListing> found;
    for (auto const& d : list_display_drivers(global_system())) {
        auto const text = debug(d);
        if (!found && text.find(dev_arg) != std::string::npos)
            found = d;
        fmt::print("{} {}\n", (found == d) ? "=>" : "  ", debug(d));
    }
    fmt::print("\n");

    if (!found) throw std::runtime_error("No matching device");
    return open_display_driver(global_system(), found->dev_file);
}

DisplayScreen find_screen(
    std::unique_ptr<DisplayDriver> const& driver, std::string const& screen_arg
) {
    if (!driver) return {};

    fmt::print("=== Video screen connectors ===\n");
    DisplayScreen found = {};
    for (auto const& screen : driver->scan_screens()) {
        if (
            found.connector.empty() &&
            screen.connector.find(screen_arg) != std::string::npos
        ) {
            found = screen;
        }

        fmt::print(
            "{} Screen #{:<3} {}{}\n",
            found.id == screen.id ? "=>" : "  ", screen.id, screen.connector,
            screen.display_detected ? " [connected]" : " [no connection]"
        );
    }
    fmt::print("\n");

    if (!found.id) throw std::runtime_error("No matching screen");
    return found;
}

DisplayMode find_mode(
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayScreen const& screen,
    std::string const& mode_arg
) {
    if (!driver || !screen.id) return {};

    fmt::print("=== Video modes ===\n");
    std::set<std::string> seen;
    DisplayMode found = mode_arg.empty() ? screen.active_mode : DisplayMode{};
    for (auto const& mode : screen.modes) {
        std::string const mode_str = debug(mode);
        if (found.name.empty() && mode_str.find(mode_arg) != std::string::npos)
            found = mode;

        if (seen.insert(mode.name).second) {
            fmt::print(
                "{} {}{}\n",
                found.name == mode.name ? "=>" : "  ", mode_str,
                screen.active_mode.name == mode.name ? " [on]" : ""
            );
        }
    }
    fmt::print("\n");

    if (found.name.empty()) throw std::runtime_error("No matching mode");
    return found;
}

Script load_script(std::string const& filename) {
    main_logger()->info("Loading script: {}", filename);

    std::ifstream ifs;
    ifs.exceptions(~std::ifstream::goodbit);
    ifs.open(filename, std::ios::binary);
    std::string const text(
        (std::istreambuf_iterator<char>(ifs)),
        (std::istreambuf_iterator<char>())
    );

    try {
        auto const json = nlohmann::json::parse(text);
        return json.get<Script>();
    } catch (nlohmann::json::parse_error const& je) {
        throw_with_nested(std::runtime_error(je.what()));
    }
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
        if (ex.si_status) throw(std::runtime_error("Kernel debug chmod error"));
    }

    auto const fd = global_system()->open(debug_file, O_WRONLY).ex(debug_file);
    auto const val = fmt::format("0x{:x}", enable ? 0x3DF : 0);
    fmt::print("Kernel debug: Writing {} to {}\n\n", val, debug_file);
    fd->write(val.data(), val.size()).ex(debug_file);
}

void play_video(
    std::string const& media_file, 
    std::string const& overlay_file,
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayScreen const& screen,
    DisplayMode const& mode,
    double start_arg,
    double buffer_arg,
    double overlay_opacity_arg
) {
    using namespace std::chrono_literals;
    auto const logger = main_logger();
    auto const sys = global_system();

    DisplayLayer overlay_layer = {};
    if (!overlay_file.empty()) {
        logger->info("Loading overlay: {}", overlay_file);
        auto const overlay = open_media_decoder(overlay_file);
        std::optional<MediaFrame> frame = overlay->next_frame();
        if (!frame)
            throw std::runtime_error("No frames in overlay media");

        overlay_layer.image = driver->load_image(frame->image);
        overlay_layer.from_size = frame->image.size.as<double>();
        overlay_layer.to_xy = (mode.size - frame->image.size) / 2;
        overlay_layer.to_size = frame->image.size;
        overlay_layer.opacity = overlay_opacity_arg;
    }

    std::shared_ptr const signal = make_signal();
    auto const loader = make_frame_loader(driver.get(), media_file);
    auto const player = start_frame_player(sys, driver.get(), screen.id, mode);

    logger->info("Start at {:.3f} seconds...", start_arg);
    auto const start_time = sys->steady_time() - Seconds(start_arg);

    for (;;) {
        auto now = sys->steady_time();
        TRACE(logger, "UPDATE at {:.3}", now.time_since_epoch());

        Interval<Seconds> req;
        req.begin = std::max(player->last_shown() + 0.001s - start_time, 0.0s);
        req.end = std::max(req.begin, now - start_time) + Seconds(buffer_arg);

        IntervalSet<Seconds> req_set;
        req_set.insert(req);
        loader->set_request(req_set, signal);

        auto const loaded = loader->content();
        if (loaded.eof && now - start_time >= *loaded.eof) break;

        FramePlayer::Timeline timeline;
        for (auto const& [frame_time, frame_image] : loaded.frames) {
            DisplayLayer frame_layer = {};
            frame_layer.image = frame_image;
            frame_layer.from_size = frame_image->size().as<double>();
            frame_layer.to_size = mode.size;

            std::vector<DisplayLayer> layers;
            layers.push_back(std::move(frame_layer));
            if (overlay_layer.image) layers.push_back(overlay_layer);
            timeline[start_time + frame_time] = std::move(layers);
        }
        player->set_timeline(timeline, signal);

        signal->wait_until(now + Seconds(std::max(0.020, buffer_arg / 5)));
    }

    logger->info("End of playback");
}

}  // namespace

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    double buffer_arg = 0.1;
    std::string dev_arg;
    std::string screen_arg;
    std::string log_arg;
    std::string mode_arg;
    std::string media_arg;
    std::string overlay_arg;
    std::string script_arg;
    double overlay_opacity_arg = 1.0;
    double start_arg = -0.2;
    bool debug_libav = false;
    bool debug_kernel = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--buffer", buffer_arg, "Seconds of readahead");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--screen", screen_arg, "Video output connector");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--overlay", overlay_arg, "Image file to overlay");
    app.add_option("--overlay_opacity", overlay_opacity_arg, "Overlay alpha");
    app.add_option("--start", start_arg, "Seconds into media to start");
    app.add_option("--sleep", sleep_arg, "Seconds to wait before exiting");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    app.add_flag("--debug_kernel", debug_kernel, "Enable kernel DRM debugging");

    auto input = app.add_option_group("Input")->require_option(0, 1);
    input->add_option("--media", media_arg, "Media file to play");
    input->add_option("--script", script_arg, "Script file to play");

    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);
    pivid::set_kernel_debug(debug_kernel);

    try {
        if (!media_arg.empty() && !script_arg.empty()) {
            throw std::runtime_error("");
        }

        auto const driver = find_driver(dev_arg);
        auto const screen = find_screen(driver, screen_arg);
        auto const mode = find_mode(driver, screen, mode_arg);

        if (!media_arg.empty() && driver) {
            play_video(
                media_arg, overlay_arg, driver, screen, mode,
                start_arg, buffer_arg, overlay_opacity_arg
            );
        }

        if (!script_arg.empty()) {
            auto script = load_script(script_arg);
        }

        if (sleep_arg > 0) {
            fmt::print("Sleeping {:.1f} seconds...", sleep_arg);
            std::chrono::duration<double> sleep_time{sleep_arg};
            std::this_thread::sleep_for(sleep_time);
        }
    } catch (std::exception const& e) {
        main_logger()->critical("{}", e.what());
    }

    fmt::print("Done!\n\n");
    return 0;
}

}  // namespace pivid
