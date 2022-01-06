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
    DisplayMode found = {};
    for (auto const& mode : conn.display_modes) {
        auto const mode_str = debug_string(mode);
        if (mode.name.empty() && mode_str.find(mode_arg) != std::string::npos)
            found = mode;

        if (seen.insert(mode.name).second) {
            fmt::print(
                "  {} {}{}\n",
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
    auto const& info = decoder->info();
    fmt::print(
       "Format: {} : {} : {}\n", 
       info.container_type, info.codec_name, info.pixel_format
    );

    fmt::print("Stream:");
    if (info.width && info.height)
        fmt::print(" {}x{}", *info.width, *info.height);
    if (info.frame_rate) fmt::print(" @{:.2f}fps", *info.frame_rate);
    if (info.duration) fmt::print(" {:.1f}sec", *info.duration);
    if (info.bit_rate) fmt::print(" {:.3f}Mbps", *info.bit_rate * 1e-6);
    fmt::print("\n\n");

    return decoder;
}

void print_frame(MediaFrame const& frame) {
    fmt::print("{:5.3f}s", frame.time);
    if (!frame.frame_type.empty())
        fmt::print(" {:<2s}", frame.frame_type);

    for (auto const& image : frame.layers) {
        fmt::print(
            " [{}x{} {:.4s}",
            image.width, image.height, (char const*) &image.fourcc
        );

        if (image.modifier) {
            auto const vendor = image.modifier >> 56;
            switch (vendor) {
#define V(x) case DRM_FORMAT_MOD_VENDOR_##x: fmt::print(":{}", #x); break
                V(NONE);
                V(INTEL);
                V(AMD);
                V(NVIDIA);
                V(SAMSUNG);
                V(QCOM);
                V(VIVANTE);
                V(BROADCOM);
                V(ARM);
                V(ALLWINNER);
                V(AMLOGIC);
#undef V
                default: fmt::print(":#{}", vendor);
            }
            fmt::print(
                ":{:x}", image.modifier & ((1ull << 56) - 1)
            );
        }

        for (auto const& chan : image.channels) {
            fmt::print(
                "{}{}bpp",
                (&chan != &image.channels[0]) ? " | " : " ",
                8 * chan.line_stride / image.width
            );
            if (chan.memory_offset > 0)
                fmt::print(" @{}k", chan.memory_offset / 1024);
        }

        fmt::print("]");
    }
    if (frame.is_corrupt) fmt::print(" CORRUPT");
    if (frame.is_key_frame) fmt::print(" KEY");
    fmt::print("\n");
}

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg = "gpu";
    std::string conn_arg;
    std::string mode_arg;
    std::string media_arg;
    bool debug_libav = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video output");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file or URL");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    CLI11_PARSE(app, argc, argv);

    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    try {
        using namespace std::chrono_literals;
        auto const driver = find_driver(dev_arg);
        auto const conn = find_connector(driver, conn_arg);
        auto const mode = find_mode(driver, conn, mode_arg);
        auto const decoder = find_media(media_arg);

        if (driver && conn.id && !mode.name.empty() && !decoder) {
            fmt::print("Setting mode \"{}\"...\n", mode.name);
            driver->update_output(conn.id, mode, {});
            while (!driver->ready_for_update(conn.id))
                std::this_thread::sleep_for(0.01s);

            fmt::print("  Mode set complete.\n\n");
        }

        while (decoder && !decoder->reached_eof()) {
            if (!decoder->next_frame_ready())
                std::this_thread::sleep_for(0.005s);

            auto const frame = decoder->get_next_frame();
            print_frame(frame);

            if (driver) {
                std::vector<DisplayLayer> display_layers;
                for (auto const& image : frame.layers) {
                    DisplayLayer display_layer = {};
                    display_layer.image = image;
                    display_layer.image_width = image.width;
                    display_layer.image_height = image.height;
                    display_layer.screen_width = mode.horiz.display;
                    display_layer.screen_height = mode.vert.display;
                    display_layers.push_back(display_layer);
                }

                while (!driver->ready_for_update(conn.id))
                    std::this_thread::sleep_for(0.005s);

                driver->update_output(conn.id, mode, display_layers);
            }
        }

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
