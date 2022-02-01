// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

extern "C" {
#include <libavutil/log.h>
}

#include "display_output.h"
#include "logging_policy.h"
#include "media_decoder.h"

namespace pivid {

namespace {

std::shared_ptr<spdlog::logger> const& main_logger() {
    static const auto logger = make_logger("main");
    return logger;
}

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    if (dev_arg == "none" || dev_arg == "/dev/null") return {};

    fmt::print("=== Video drivers ===\n");
    auto sys = global_system();
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

DisplayConnectorStatus find_connector(
    std::unique_ptr<DisplayDriver> const& driver, std::string const& conn_arg
) {
    if (!driver) return {};

    fmt::print("=== Video display connectors ===\n");
    DisplayConnectorStatus found = {};
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
    DisplayConnectorStatus const& conn,
    std::string const& mode_arg
) {
    if (!driver || !conn.id) return {};

    fmt::print("=== Video modes ===\n");
    std::set<std::string> seen;
    DisplayMode found = mode_arg.empty() ? conn.active_mode : DisplayMode{};
    for (auto const& mode : conn.display_modes) {
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
    auto decoder = new_media_decoder(media_arg);
    return decoder;
}

void play_video(
    std::unique_ptr<MediaDecoder> const& decoder,
    std::unique_ptr<MediaDecoder> const& overlay,
    std::string const& tiff_arg,
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnectorStatus const& conn,
    DisplayMode const& mode
) {
    using namespace std::chrono_literals;
    auto const log = main_logger();

    std::vector<DisplayUpdateRequest::Layer> overlays;
    if (overlay) {
        log->trace("Loading overlay image...");
        std::optional<MediaFrame> overlay_frame = overlay->next_frame();
        if (!overlay_frame)
            throw std::runtime_error("No frames in overlay media");

        for (auto const& image : overlay_frame->images) {
            DisplayUpdateRequest::Layer layer = {};
            layer.loaded_image = driver->load_image(image);
            layer.source_width = image.width;
            layer.source_height = image.height;
            layer.screen_x = (mode.horiz.display - image.width) / 2;
            layer.screen_y = (mode.vert.display - image.height) / 2;
            layer.screen_width = image.width;
            layer.screen_height = image.height;
            overlays.push_back(std::move(layer));
        }
    }

    auto last_wall = std::chrono::steady_clock::now();
    double last_frame = 0.0;
    double behind = 0.0;
    int frame_index = 0;

    log->trace("Getting first video frame...");
    while (decoder) {
        auto const frame = decoder->next_frame();
        if (!frame) break;

        if (driver) {
            DisplayUpdateRequest request;
            request.connector_id = conn.id;
            request.mode = mode;

            for (auto const& image : frame->images) {
                DisplayUpdateRequest::Layer layer = {};
                layer.loaded_image = driver->load_image(image);
                layer.source_width = image.width;
                layer.source_height = image.height;
                layer.screen_width = mode.horiz.display;
                layer.screen_height = mode.vert.display;
                request.layers.push_back(std::move(layer));
            }

            request.layers.insert(
                request.layers.end(), overlays.begin(), overlays.end()
            );

            while (!driver->is_update_done(conn.id)) {
                log->trace("Sleeping for display ({})...", conn.name);
                std::this_thread::sleep_for(0.001s);
            }

            driver->request_update(request);
        }

        log->trace("Printing stats...");
        auto const wall = std::chrono::steady_clock::now();
        auto const dw = std::chrono::duration<double>(wall - last_wall).count();
        auto const df = frame->time - last_frame;
        last_wall = wall;
        last_frame = frame->time;
        behind = std::max(0.0, behind + dw - df);
        fmt::print(
            "Frame {:>3.0f}/{:.0f}ms {:>3.0f}beh {}\n",
            dw * 1000, df * 1000, behind * 1000, debug(*frame)
        );

        if (!tiff_arg.empty()) {
            auto dot_pos = tiff_arg.rfind('.');
            if (dot_pos == std::string::npos) dot_pos = tiff_arg.size();
            for (size_t i = 0; i < frame->images.size(); ++i) {
                log->trace("Encoding TIFF...");
                auto path = tiff_arg.substr(0, dot_pos);
                path += fmt::format(".F{:05d}", frame_index);
                if (frame->images.size() > 1)
                    path += fmt::format(".I{}", i);
                path += tiff_arg.substr(dot_pos);
                auto tiff = debug_tiff(frame->images[i]);

                log->trace("Writing TIFF...");
                std::ofstream ofs;
                ofs.exceptions(~std::ofstream::goodbit);
                ofs.open(path, std::ios::binary);
                ofs.write((char const*) tiff.data(), tiff.size());
                log->trace("Saving: {}", path);
            }
        }

        ++frame_index;
        log->trace("Getting next video frame (#{})...", frame_index);
    }

    while (driver && !driver->is_update_done(conn.id)) {
        log->trace("Sleeping for final display ({})...", conn.name);
        std::this_thread::sleep_for(0.005s);
    }

    log->debug("End of media file reached");
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
    std::string tiff_arg;
    bool debug_libav = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video output");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file to play");
    app.add_option("--overlay", overlay_arg, "Image file to overlay");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    app.add_option("--save_tiff", tiff_arg, "Save frames as .tiff");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    try {
        auto const driver = find_driver(dev_arg);
        auto const conn = find_connector(driver, conn_arg);
        auto const mode = find_mode(driver, conn, mode_arg);
        auto const decoder = find_media(media_arg);
        auto const overlay = find_media(overlay_arg);
        play_video(decoder, overlay, tiff_arg, driver, conn, mode);

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
