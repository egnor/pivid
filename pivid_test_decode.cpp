// Simple command line tool to exercise video decoding and playback.

#include <drm_fourcc.h>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include <chrono>
#include <cmath>
#include <thread>

#include "display_output.h"
#include "media_decoder.h"

using namespace std::chrono_literals;

// Main program, parses flags and calls the decoder loop.
int main(int const argc, char const* const* const argv) {
    std::string dev_arg = "gpu";
    std::string conn_arg;
    std::string mode_arg;
    std::string media_arg;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video mode");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file or URL");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    CLI11_PARSE(app, argc, argv);

    try {
        auto* sys = pivid::global_system();

        fmt::print("=== Video drivers ===\n");
        std::string dev_file;
        for (auto const& d : pivid::list_display_drivers(sys)) {
            if (
                dev_file.empty() && (
                    d.dev_file.find(dev_arg) != std::string::npos ||
                    d.system_path.find(dev_arg) != std::string::npos ||
                    d.driver.find(dev_arg) != std::string::npos ||
                    d.driver_bus_id.find(dev_arg) != std::string::npos
                )
            ) {
                dev_file = d.dev_file;
            }
            fmt::print(
                "{} {}\n", (dev_file == d.dev_file) ? "=>" : "  ",
                pivid::debug_string(d)
            );
        }
        fmt::print("\n");

        if (dev_file.empty()) throw std::runtime_error("No matching device");
        auto const driver = pivid::open_display_driver(sys, dev_file);

        fmt::print("=== Display output connectors ===\n");
        uint32_t connector_id = 0;
        pivid::DisplayMode mode = {};
        for (auto const& output : driver->scan_outputs()) {
            if (
                !connector_id &&
                output.connector_name.find(conn_arg) != std::string::npos
            ) {
                connector_id = output.connector_id;
                if (mode_arg.empty()) mode = output.active_mode;
            }

            fmt::print(
                "{} Conn #{:<3} {}{}\n",
                connector_id == output.connector_id ? "=>" : "  ",
                output.connector_id, output.connector_name,
                output.display_detected ? " [connected]" : " [no connection]"
            );

            std::set<std::string> seen;
            for (auto const& display_mode : output.display_modes) {
                auto const mode_str = pivid::debug_string(display_mode);
                if (
                    mode.name.empty() &&
                    connector_id == output.connector_id &&
                    mode_str.find(mode_arg) != std::string::npos
                ) {
                    mode = display_mode;
                }

                if (seen.insert(display_mode.name).second) {
                    fmt::print(
                        "  {} {}{}\n",
                        mode.name == display_mode.name ? "=>" : "  ", mode_str,
                        output.active_mode.name == display_mode.name
                            ? " [on]" : ""
                    );
                }
            }
            fmt::print("\n");
        }

        if (!connector_id) throw std::runtime_error("No matching connector");
        if (mode.name.empty()) throw std::runtime_error("No matching mode");

        fmt::print("Setting mode \"{}\"...\n", mode.name);
        driver->update_output(connector_id, mode, {});
        while (!driver->ready_for_update(connector_id)) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(0.01s);
        }
        fmt::print("  Mode set complete.\n\n");

        if (!media_arg.empty()) {
            fmt::print("=== Media playback ({}) ===\n", media_arg);
            auto const decoder = pivid::new_media_decoder(media_arg);
            auto const& info = decoder->info();
            fmt::print(
               "{} : {} : {}\n", 
               info.container_type, info.codec_name, info.pixel_format
            );

            if (info.duration) fmt::print("{:.1f}sec", info.duration);
            if (info.frame_count) fmt::print(" ({} frames)", info.frame_count);
            if (info.frame_rate) fmt::print(" @{:.2f}fps", info.frame_rate);
            if (info.bit_rate) fmt::print(" {:.3f}Mbps", info.bit_rate * 1e-6);
            fmt::print(" {}x{}\n", info.width, info.height);

            for (;;) {
                while (!decoder->next_frame_ready())
                    std::this_thread::sleep_for(0.01s);

                auto const frame = decoder->next_frame();
                if (frame.at_eof) break;

                fmt::print("{:5.2f}s", frame.time);
                if (!frame.frame_type.empty())
                    fmt::print(" {:<2s}", frame.frame_type);
                for (auto const fb : frame.layers) {
                    fmt::print(
                        " [{}x{} {:.4s}",
                        fb->width, fb->height, (char const*) &fb->fourcc
                    );

                    if (fb->modifier) {
                        auto const vendor = fb->modifier >> 56;
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
                            ":{:x}", fb->modifier & ((1ull << 56) - 1)
                        );
                    }

                    for (auto const& chan : fb->channels) {
                        fmt::print(
                            "{}{}bpp fd{:<2d}",
                            (&chan != &fb->channels[0]) ? " | " : " ",
                            8 * chan.bytes_per_line / fb->width, chan.dma_fd
                        );
                        if (chan.start_offset > 0)
                            fmt::print(" @{}k", chan.start_offset / 1024);
                    }

                    fmt::print("]");
                }
                if (frame.is_corrupt) fmt::print(" CORRUPT");
                if (frame.is_key_frame) fmt::print(" KEY");
                fmt::print("\n");

                std::vector<pivid::DisplayLayer> display_layers;
                for (auto const fb : frame.layers) {
                    pivid::DisplayLayer display_layer = {};
                    display_layer.fb = fb;
                    display_layer.fb_width = fb->width;
                    display_layer.fb_height = fb->height;
                    display_layer.out_width = mode.horiz.display;
                    display_layer.out_height = mode.vert.display;
                    display_layers.push_back(display_layer);
                }

                while (!driver->ready_for_update(connector_id))
                    std::this_thread::sleep_for(0.01s);

                driver->update_output(connector_id, mode, display_layers);
            }
        }

        if (sleep_arg > 0) {
            fmt::print("Sleeping {:.1f} seconds...\n", sleep_arg);
            std::this_thread::sleep_for(
               std::chrono::duration<double>(sleep_arg)
            );
        }

        fmt::print("Done!\n\n");
    } catch (std::exception const& e) {
        fmt::print("*** {}\n", e.what());
    }

    return 0;
}
