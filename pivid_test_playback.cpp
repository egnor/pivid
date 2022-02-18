// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>

extern "C" {
#include <libavutil/log.h>
}

#include "display_output.h"
#include "frame_loader.h"
#include "frame_player.h"
#include "logging_policy.h"
#include "media_decoder.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& main_logger() {
    static const auto logger = make_logger("main");
    return logger;
}

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    if (dev_arg == "none" || dev_arg == "/dev/null") return {};

    fmt::print("=== Video drivers ===\n");
    auto const sys = global_system();
    std::string found;
    for (auto const& d : list_display_drivers(sys)) {
        if (
            found.empty() && (
                d.dev_file.find(dev_arg) != std::string::npos ||
                d.system_path.find(dev_arg) != std::string::npos ||
                d.driver.find(dev_arg) != std::string::npos ||
                d.driver_bus_id.find(dev_arg) != std::string::npos
            )
        ) {
            found = d.dev_file;
        }
        fmt::print("{} {}\n", (found == d.dev_file) ? "=>" : "  ", debug(d));
    }
    fmt::print("\n");

    if (found.empty()) throw std::runtime_error("No matching device");
    return open_display_driver(sys, found);
}

DisplayConnector find_connector(
    std::unique_ptr<DisplayDriver> const& driver, std::string const& conn_arg
) {
    if (!driver) return {};

    fmt::print("=== Video display connectors ===\n");
    DisplayConnector found = {};
    for (auto const& conn : driver->scan_connectors()) {
        if (found.name.empty() && conn.name.find(conn_arg) != std::string::npos)
            found = conn;

        fmt::print(
            "{} Conn #{:<3} {}{}\n",
            found.id == conn.id ? "=>" : "  ", conn.id, conn.name,
            conn.display_detected ? " [connected]" : " [no connection]"
        );
    }
    fmt::print("\n");

    if (!found.id) throw std::runtime_error("No matching connector");
    return found;
}

DisplayMode find_mode(
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnector const& conn,
    std::string const& mode_arg
) {
    if (!driver || !conn.id) return {};

    fmt::print("=== Video modes ===\n");
    std::set<std::string> seen;
    DisplayMode found = mode_arg.empty() ? conn.active_mode : DisplayMode{};
    for (auto const& mode : conn.modes) {
        std::string const mode_str = debug(mode);
        if (found.name.empty() && mode_str.find(mode_arg) != std::string::npos)
            found = mode;

        if (seen.insert(mode.name).second) {
            fmt::print(
                "{} {}{}\n",
                found.name == mode.name ? "=>" : "  ", mode_str,
                conn.active_mode.name == mode.name ? " [on]" : ""
            );
        }
    }
    fmt::print("\n");

    if (found.name.empty()) throw std::runtime_error("No matching mode");
    return found;
}

std::unique_ptr<MediaDecoder> find_media(std::string const& media_arg) {
    if (media_arg.empty()) return {};

    fmt::print("=== Playing media ({}) ===\n", media_arg);
    auto decoder = open_media_decoder(media_arg);
    return decoder;
}

void play_video(
    std::unique_ptr<MediaDecoder> decoder,
    std::unique_ptr<MediaDecoder> overlay,
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnector const& conn,
    DisplayMode const& mode,
    double start_arg
) {
    using namespace std::chrono_literals;
    auto const logger = main_logger();
    auto const sys = global_system();

    DisplayLayer overlay_layer = {};
    if (overlay) {
        logger->trace("Loading overlay image...");
        std::optional<MediaFrame> frame = overlay->next_frame();
        if (!frame)
            throw std::runtime_error("No frames in overlay media");

        overlay_layer.image = driver->load_image(frame->image);
        overlay_layer.from_size = frame->image.size.as<double>();
        overlay_layer.to = (mode.size - frame->image.size) / 2;
        overlay_layer.to_size = frame->image.size;
    }

    auto const player = start_frame_player(sys, driver.get(), conn.id, mode);
    auto const loader = make_frame_loader(driver.get());
    auto const window = loader->open_window(std::move(decoder));

    logger->info("Offset {:.3f} seconds...", start_arg);
    auto const start_time = sys->steady_time() - Seconds(start_arg);

    for (;;) {
        auto now = sys->steady_time();
        logger->trace("UPDATE at {:.3}", now.time_since_epoch());

        FrameWindow::Request req = {};
        req.begin = std::max(player->last_shown() + 0.001s - start_time, 0.0s);
        req.end = now - start_time + 100ms;
        window->set_request(req);

        auto const progress = window->load_progress();
        auto const frames = window->loaded();
        if (frames.empty() && progress >= FrameWindow::eof) break;

        FramePlayer::Timeline timeline;
        for (auto const& time_frame : frames) {
            DisplayLayer frame_layer = {};
            frame_layer.image = time_frame.second;
            frame_layer.from_size = time_frame.second->size().as<double>();
            frame_layer.to_size = mode.size;

            std::vector<DisplayLayer> layers;
            layers.push_back(std::move(frame_layer));
            if (overlay_layer.image) layers.push_back(overlay_layer);
            timeline[start_time + time_frame.first] = std::move(layers);
        }
        player->set_timeline(timeline);
        sys->wait_until(now + 10ms);
    }

    logger->info("End of playback");
}

}  // namespace

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg;
    std::string conn_arg;
    std::string log_arg;
    std::string mode_arg;
    std::string media_arg;
    std::string overlay_arg;
    double start_arg = -0.1;
    bool debug_libav = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video output");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file to play");
    app.add_option("--overlay", overlay_arg, "Image file to overlay");
    app.add_option("--start", start_arg, "Start this many seconds into media");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    try {
        auto const driver = find_driver(dev_arg);
        auto const conn = find_connector(driver, conn_arg);
        auto const mode = find_mode(driver, conn, mode_arg);
        auto decoder = find_media(media_arg);
        auto overlay = find_media(overlay_arg);

        if (decoder && driver) {
            play_video(
                std::move(decoder),
                std::move(overlay),
                driver, conn, mode,
                start_arg
            );
        }

        if (sleep_arg > 0) {
            fmt::print("Sleeping {:.1f} seconds...", sleep_arg);
            std::chrono::duration<double> sleep_time{sleep_arg};
            std::this_thread::sleep_for(sleep_time);
        }

    } catch (std::exception const& e) {
        main_logger()->critical("*** {}", e.what());
    }

    fmt::print("Done!\n\n");
    return 0;
}

}  // namespace pivid
