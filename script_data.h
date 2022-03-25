#pragma once

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
    std::string mode;
    std::vector<ScriptLayer> layers;
};

struct Script {
    std::map<std::string, ScriptScreen> screens;
    std::vector<ScriptMedia> standbys;
};

void from_json(nlohmann::json const&, Script&);

}  // namespace pivid
