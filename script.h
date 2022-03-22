#pragma once

#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "bezier_spline.h"
#include "xy.h"

namespace pivid {

struct ScriptLayer {
    std::string media_file;
    BezierSpline frame_time;
    XY<BezierSpline> from, from_size;
    XY<BezierSpline> to, to_size;
    BezierSpline opacity;
};

struct ScriptScreen {
    XY<int> mode_size;
    std::vector<ScriptLayer> layers;
};

struct Script {
    std::map<std::string, ScriptScreen> screens;
    std::vector<ScriptLayer> standby_layers;
};

void from_json(nlohmann::json const&, Script&);

}  // namespace pivid
