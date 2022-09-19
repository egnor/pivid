// Structures to hold parsed play scripts, and functions to parse
// those scripts from JSON text.

#pragma once

#include <limits>
#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "bezier_spline.h"
#include "xy.h"

namespace pivid {

// A begin/end preload directive for a given media file.
struct ScriptPreload {
    BezierSpline begin;
    BezierSpline end;
};

// Options applied to a media file, including preload directives.
struct ScriptBufferTuning {
    std::vector<ScriptPreload> pin;
    double decoder_idle_time = 1.0;
    double seek_scan_time = 1.0;
};

// Video mode specification, including resolution and refresh rate.
struct ScriptMode {
    XY<int> size;
    int hz = 0;
    auto operator<=>(ScriptMode const&) const = default;
};

// One item to layer into the output screen, sourced from a media file,
// with Bezier-specified position in the file and on the screen.
struct ScriptLayer {
    std::string media;
    BezierSpline play;
    double buffer = 0.2;
    XY<BezierSpline> from_xy, from_size;
    XY<BezierSpline> to_xy, to_size;
    BezierSpline opacity;
    bool reflect;
    int rotate;
};

// Description of what to render on a particular screen.
struct ScriptScreen {
    ScriptMode mode;
    double update_hz = 0.0;  // How often to change screen content
    std::vector<ScriptLayer> layers;  // What to show
};

// An entire parsed play script, including global parameters and all screens.
struct Script {
    std::map<std::string, ScriptBufferTuning> buffer_tuning;  // Per-file config
    std::map<std::string, ScriptScreen> screens;  // Contents by connector name
    double zero_time = 0.0;         // Make all timestamps relative to this
    double main_loop_hz = 30.0;     // Refresh frame timelines this often
};

// Returns a script parsed from text.
// If the script does not specify zero_time, default_zero_time is used.
Script parse_script(std::string_view, double default_zero_time);

}  // namespace pivid
