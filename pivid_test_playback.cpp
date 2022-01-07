// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <thread>

#include <drm_fourcc.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

extern "C" {
#include <libavutil/log.h>
}

#include "display_output.h"
#include "media_decoder.h"

namespace pivid {

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    if (dev_arg == "none" || dev_arg == "/dev/null" || dev_arg == "") return {};

    fmt::print("=== Video drivers ===\n");
    std::string found;
    auto* sys = global_system();
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
        fmt::print(
            "{} {}\n", (found == d.dev_file) ? "=>" : "  ", debug_string(d)
        );
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
    for (auto const& mode : conn.display_modes) {
        auto const mode_str = debug_string(mode);
        if (mode.name.empty() && mode_str.find(mode_arg) != std::string::npos)
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

    fmt::print("=== Media file ({}) ===\n", media_arg);
    auto decoder = new_media_decoder(media_arg);
    fmt::print("Format: {}\n\n", debug_string(decoder->info()));
    return decoder;
}

void play_video(
    std::unique_ptr<MediaDecoder> const& decoder,
    std::unique_ptr<MediaDecoder> const& overlay,
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnector const& conn,
    DisplayMode const& mode
) {
    using namespace std::chrono_literals;
    MediaFrame overlay_frame = {};
    if (overlay) {
        while (!overlay->next_frame_ready()) {
            if (overlay->reached_eof())
                throw std::runtime_error("No frames in overlay media");
            std::this_thread::sleep_for(0.005s);
        }
        overlay_frame = overlay->get_next_frame();
    }

    auto last_wall = std::chrono::steady_clock::now();
    double last_frame = 0.0;
    double behind = 0.0;
    while (decoder && !decoder->reached_eof()) {
        if (!decoder->next_frame_ready())
            std::this_thread::sleep_for(0.005s);

        auto const frame = decoder->get_next_frame();

        if (driver) {
            std::vector<DisplayLayer> display_layers;
            for (auto const& image : frame.layers) {
                DisplayLayer display_layer = {};
                display_layer.image = image;
                display_layer.image_width = image.width;
                display_layer.image_height = image.height;
                display_layer.screen_width = mode.horiz.display;
                display_layer.screen_height = mode.vert.display;
                display_layers.push_back(std::move(display_layer));
            }

            for (auto const& image : overlay_frame.layers) {
                DisplayLayer display_layer = {};
                display_layer.image = image;
                display_layer.image_width = image.width;
                display_layer.image_height = image.height;
                display_layer.screen_x =
                    (mode.horiz.display - image.width) / 2;
                display_layer.screen_y =
                    (mode.vert.display - image.height) / 2;
                display_layer.screen_width = image.width;
                display_layer.screen_height = image.height;
                display_layers.push_back(std::move(display_layer));
            }

            while (!driver->ready_for_update(conn.id)) {  // Wait for flip.
                decoder->next_frame_ready();  // Keep decoder running.
                std::this_thread::sleep_for(0.005s);
            }
            driver->update_output(conn.id, mode, display_layers);
        }

        auto const wall = std::chrono::steady_clock::now();
        auto const dw = std::chrono::duration<double>(wall - last_wall).count();
        auto const df = frame.time - last_frame;
        last_wall = wall;
        last_frame = frame.time;
        behind = std::max(0.0, behind + dw - df);
        
        fmt::print(
            "{:>3.0f}/{:.0f}ms {:>3.0f}beh {}\n",
            dw * 1000, df * 1000, behind * 1000,
            debug_string(frame)
        );
    }
}

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg = "gpu";
    std::string conn_arg;
    std::string mode_arg;
    std::string media_arg;
    std::string overlay_arg;
    bool debug_libav = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video output");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file to play");
    app.add_option("--overlay", overlay_arg, "Image file to overlay");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    CLI11_PARSE(app, argc, argv);

    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    try {
        auto const driver = find_driver(dev_arg);
        auto const conn = find_connector(driver, conn_arg);
        auto const mode = find_mode(driver, conn, mode_arg);
        auto const decoder = find_media(media_arg);
        auto const overlay = find_media(overlay_arg);
        play_video(decoder, overlay, driver, conn, mode);

        if (sleep_arg > 0) {
            fmt::print("Sleeping {:.1f} seconds...\n", sleep_arg);
            std::chrono::duration<double> sleep_time{sleep_arg};
            std::this_thread::sleep_for(sleep_time);
        }

        fmt::print("Done!\n\n");
    } catch (std::exception const& e) {
        fmt::print("*** {}\n", e.what());
    }

    return 0;
}

}  // namespace pivid
