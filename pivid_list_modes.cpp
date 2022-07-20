// Simple command line tool to print standard modes.

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include "display_mode.h"
#include "logging_policy.h"

namespace pivid {

static std::vector<DisplayMode> filter_modes(
    std::vector<DisplayMode> const& modes, XY<int> xy, int hz
) {
    std::vector<DisplayMode> out;
    for (auto const& m : modes) {
        if (
            (!xy.x || xy.x == m.size.x) && (!xy.y || xy.y == m.size.y) &&
            (!hz || hz == m.nominal_hz)
        ) {
            out.push_back(m);
        }
    }
    return out;
}

// Main program, parses flags and prints modes.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string log_arg;
    XY<int> size = {0, 0};
    int hz = 0;

    CLI::App app("Print video drivers, connectors and modes");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--x", size.x, "Mode width");
    app.add_option("--y", size.y, "Mode height");
    app.add_option("--hz", hz, "Mode frequency");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    auto const logger = make_logger("pivid_list_modes");

    auto const cta_modes = filter_modes(cta_861_modes, size, hz);
    auto const dmt_modes = filter_modes(vesa_dmt_modes, size, hz);
    auto const cvt_mode = vesa_cvt_mode(size, hz ? hz : 60);
    auto const cvt_rb_mode = vesa_cvt_rb_mode(size, hz ? hz : 60);

    auto const spec = fmt::format(
        "{}x{} {}hz",
        size.x ? fmt::format("{}", size.x) : "?",
        size.y ? fmt::format("{}", size.y) : "?",
        hz ? fmt::format("{}", hz) : "?"
    );

    if (cta_modes.empty()) {
        fmt::print("*** No CTA-861 modes match {}\n", spec);
    } else {
        fmt::print("=== CTA-861 'TV' modes for {} ===\n", spec);
        for (auto const& mode : cta_modes)
            fmt::print("{}\n", debug(mode));
    }

    if (dmt_modes.empty()) {
        fmt::print("\n*** No VESA DMT modes match {}\n", spec);
    } else {
        fmt::print("\n=== VESA DMT 'monitor' modes for {} ===\n", spec);
        for (auto const& mode : dmt_modes)
            fmt::print("{}\n", debug(mode));
    }

    if (cvt_mode || cvt_rb_mode) {
        fmt::print("\n=== CVT synthesized modes for {} ===\n", spec);
        if (cvt_mode) fmt::print("{}\n", debug(*cvt_mode));
        if (cvt_rb_mode) fmt::print("{} [RBv2]\n", debug(*cvt_rb_mode));
    } else if (size.x && size.y) {
        fmt::print("\n*** No CVT modes for {}\n", spec);
    }

    fmt::print("\n");
}

}
