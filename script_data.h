#pragma once

#include <map>
#include <string>

#include <nlohmann/json_fwd.hpp>

#include "cubic_bezier.h"
#include "xy.h"

namespace pivid {

struct Script {
    struct Layer {
        std::string media_file;
        CubicBezier frame_time;
        XY<CubicBezier> from, from_size;
        XY<CubicBezier> to, to_size;
        CubicBezier alpha;
    };

    struct Screen {
        XY<int> mode_size;
        std::vector<Layer> layers;
    };

    std::map<std::string, Screen> screens;
    std::vector<Layer> standbys;
};

void from_json(nlohmann::json const&, Script&);

}  // namespace pivid