#include "script_data.h"

#include <exception>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pivid {

template <typename T>
void from_json(nlohmann::json const& j, XY<T>& xy) {
    j.at(0).get_to(xy.x);
    j.at(1).get_to(xy.y);
}

void from_json(nlohmann::json const& j, CubicBezier& bezier) {
    (void) j;
    bezier = {};
}

void from_json(nlohmann::json const& j, ScriptLayer& layer) {
    j.at("media_file").get_to(layer.media_file);
    j.at("frame_time").get_to(layer.frame_time);
    j.at("from").get_to(layer.from);
    j.at("from_size").get_to(layer.from_size);
    j.at("to").get_to(layer.to);
    j.at("to_size").get_to(layer.to_size);
    j.at("opacity").get_to(layer.opacity);
}

void from_json(nlohmann::json const& j, ScriptScreen& screen) {
    j.at("mode_size").get_to(screen.mode_size);
    j.at("layers").get_to(screen.layers);
}

void from_json(nlohmann::json const& j, Script& script) {
    j.at("screens").get_to(script.screens);
    j.at("standbys").get_to(script.standby_layers);
}

}  // namespace pivid
