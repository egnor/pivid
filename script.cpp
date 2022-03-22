#include "script.h"

#include <exception>
#include <limits>
#include <stdexcept>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

namespace pivid {

template <typename T>
void from_json(nlohmann::json const& j, XY<T>& xy) {
    j.at(0).get_to(xy.x);
    j.at(1).get_to(xy.y);
}

void from_json(nlohmann::json const& j, BezierSegment& seg) {
    seg = {};
    if (j.is_number()) {
        seg.end_t = numeric_limits<double>::infinity();
        seg.begin_x = seg.p1_x = seg.p2_x = seg.end_x = j.get<double>();
    } else {
        auto const t_j = j.at("t");
        auto const x_j = j.at("x");
        if (t_j.size() == 2) {
        } else {
        }
    }
}

void from_json(nlohmann::json const& j, BezierSpline& bezier) {
    bezier = {};
    if (j.is_object() && j.count("segments")) {
        j.at("segments").get_to(bezier.segments);
        bezier.repeat = j.value("repeat", 0.0);
    } else if (j.is_array() && !j.at(0).is_number()) {
        j.get_to(bezier.segments);
    } else {
        bezier.segments.resize(1);
        j.get_to(bezier.segments[0]);
    }
}

void from_json(nlohmann::json const& j, ScriptLayer& layer) {
    layer = {};
    j.at("media_file").get_to(layer.media_file);
    j.at("frame_time").get_to(layer.frame_time);
    j.at("from").get_to(layer.from);
    j.at("from_size").get_to(layer.from_size);
    j.at("to").get_to(layer.to);
    j.at("to_size").get_to(layer.to_size);
    j.at("opacity").get_to(layer.opacity);
}

void from_json(nlohmann::json const& j, ScriptScreen& screen) {
    screen = {};
    j.at("mode_size").get_to(screen.mode_size);
    j.at("layers").get_to(screen.layers);
}

void from_json(nlohmann::json const& j, Script& script) {
    script = {};
    j.at("screens").get_to(script.screens);
    j.at("standbys").get_to(script.standby_layers);
}

}  // namespace pivid
