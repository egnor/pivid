#pragma once

#include <limits>
#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "bezier_spline.h"
#include "xy.h"

namespace pivid {

struct ScriptPreload {
    BezierSpline begin;
    BezierSpline end;
};

struct ScriptMedia {
    std::vector<ScriptPreload> preload;
    double decoder_idle_time = 1.0;
    double seek_scan_time = 1.0;
};

struct ScriptMode {
    XY<int> size;
    int hz = 0;
    auto operator<=>(ScriptMode const&) const = default;
};

struct ScriptLayer {
    std::string media;
    BezierSpline play;
    double buffer = 0.2;
    XY<BezierSpline> from_xy, from_size;
    XY<BezierSpline> to_xy, to_size;
    BezierSpline opacity;
};

struct ScriptScreen {
    ScriptMode mode;
    double update_hz = 0.0;
    std::vector<ScriptLayer> layers;
};

struct Script {
    std::map<std::string, ScriptMedia> media;
    std::map<std::string, ScriptScreen> screens;
    double zero_time = 0.0;
    double main_loop_hz = 30.0;
    double main_buffer_time = 0.2;
};

Script parse_script(std::string_view, double default_zero_time);

}  // namespace pivid
