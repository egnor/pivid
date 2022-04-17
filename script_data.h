#pragma once

#include <limits>
#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "bezier_spline.h"
#include "xy.h"

namespace pivid {

struct ScriptMedia {
    std::string file;
    BezierSpline play;
    double playtime_buffer = 0.2;
    double mediatime_buffer = 0.0;
};

struct ScriptLayer {
    ScriptMedia media;
    XY<BezierSpline> from_xy, from_size;
    XY<BezierSpline> to_xy, to_size;
    BezierSpline opacity;
};

struct ScriptScreen {
    XY<int> display_mode = {0, 0};
    int display_hz = 0;
    double update_hz = 0.0;
    std::vector<ScriptLayer> layers;
};

struct Script {
    std::map<std::string, ScriptScreen> screens;
    std::vector<ScriptMedia> standbys;
    double main_loop_hz = 15.0;
    double main_buffer = 0.2;
};

void from_json(nlohmann::json const&, Script&);

}  // namespace pivid
