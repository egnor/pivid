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
    double buffer = 0.2;
};

struct ScriptLayer {
    ScriptMedia media;
    XY<BezierSpline> from_xy, from_size;
    XY<BezierSpline> to_xy, to_size;
    BezierSpline opacity;
};

struct ScriptScreen {
    XY<int> mode = {0, 0};
    int mode_hz = 0;
    double update_hz = 0.0;
    std::vector<ScriptLayer> layers;
};

struct Script {
    std::map<std::string, ScriptScreen> screens;
    std::vector<ScriptMedia> standbys;
    bool run_relative = false;
};

void from_json(nlohmann::json const&, Script&);

Script make_script_absolute(Script, double run_start);

}  // namespace pivid
