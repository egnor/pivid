#pragma once

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

    struct Display {
        XY<int> mode_size;
        std::vector<Layer> layers;
    };

    std::map<std::string, Display> displays;
};

}  // namespace pivid
