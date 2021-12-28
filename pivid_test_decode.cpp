// Simple command line tool to exercise video decoding and playback.

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include <chrono>
#include <cmath>
#include <thread>

#include "display_output.h"
#include "media_decoder.h"

// Main program, parses flags and calls the decoder loop.
int main(int const argc, char const* const* const argv) {
    std::string dev_arg = "gpu";
    std::string conn_arg;
    std::string mode_arg;
    std::string media_arg;
    double sleep_arg;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video mode");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file or URL");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    CLI11_PARSE(app, argc, argv);

    try {
        fmt::print("=== Video drivers ===\n");
        std::filesystem::path dev_file;
        for (auto const& d : pivid::list_display_drivers()) {
            if (
                dev_file.empty() && (
                    d.dev_file.native().find(dev_arg) != std::string::npos ||
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
        auto const driver = pivid::open_display_driver(dev_file);

        fmt::print("=== Display output connectors ===\n");
        uint32_t connector_id = 0;
        pivid::DisplayMode mode = {};
        for (auto const& output : driver->scan_outputs()) {
            if (
                !connector_id &&
                output.connector_name.find(conn_arg) != std::string::npos
            ) {
                connector_id = output.connector_id;
            }

            fmt::print(
                "{} Conn #{:<3} {}{}\n",
                connector_id == output.connector_id ? "=>" : "  ",
                output.connector_id, output.connector_name,
                output.display_detected ? " [connected]" : ""
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
           auto const decoder = pivid::new_media_decoder(media_arg);
           while (!decoder->at_eof()) {
               auto const frame = decoder->next_frame();
               if (frame) {
                   fmt::print("FRAME\n");
               } else {
                   using namespace std::chrono_literals;
                   std::this_thread::sleep_for(0.01s);
               }
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
