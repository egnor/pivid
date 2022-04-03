// Simple command line tool to print available drivers, connectors and modes.

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include "display_output.h"
#include "logging_policy.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& main_logger() {
    static const auto logger = make_logger("main");
    return logger;
}

DisplayScreen find_screen(
    std::shared_ptr<DisplayDriver> const& driver, std::string const& screen_arg
) {

    if (!found.id) throw std::runtime_error("No matching screen");
    return found;
}

DisplayMode find_mode(
    DisplayScreen const& screen,
    std::string const& mode_arg
) {
    if (!screen.id) return {};

    fmt::print("=== Video modes ===\n");
    std::set<XY<int>> seen;
    DisplayMode found = mode_arg.empty() ? screen.active_mode : DisplayMode{};
    for (auto const& mode : screen.modes) {
        std::string const mode_str = debug(mode);
        if (!found.nominal_hz && mode_str.find(mode_arg) != std::string::npos)
            found = mode;

        if (seen.insert(mode.size).second) {
            fmt::print(
                "{} {}{}\n",
                found.size == mode.size ? "=>" : "  ", mode_str,
                screen.active_mode.size == mode.size ? " [on]" : ""
            );
        }
    }
    fmt::print("\n");

    if (!found.nominal_hz) throw std::runtime_error("No matching mode");
    return found;
}

}  // namespace

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string log_arg;

    CLI::App app("Print video drivers, connectors and modes");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);
    pivid::set_kernel_debug(debug_kernel);

    std::shared_ptr sys = global_system();

    try {
        for (auto const& listing : list_display_drivers(sys)) {
            fmt::print("=== {} ===", debug(listing);
            auto driver = open_display_driver(sys, listing);
            for (auto const& screen : driver->scan_screens()) {
                fmt::print(
                    "Screen #{:<3} {}{}\n",
                    screen.id, screen.connector,
                    screen.display_detected ? " [connected]" : " [no connection]"
                );
            }
            fmt::print("\n");
        }
    } catch (std::exception const& e) {
        main_logger()->critical("{}", e.what());
    }

    fmt::print("Done!\n\n");
    return 0;
}

}  // namespace pivid
