#pragma once

#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "cubic_bezier.h"
#include "xy.h"

namespace pivid {

struct ScriptLayer {
    std::string media_file;
    CubicBezier frame_time;
    XY<CubicBezier> from, from_size;
    XY<CubicBezier> to, to_size;
    CubicBezier opacity;
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
