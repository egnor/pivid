#include "script_data.h"

#include <exception>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pivid {

void from_json(nlohmann::json const& j, ScriptScreen& screen) {
    (void) j;
    screen = {};
}

void from_json(nlohmann::json const& j, ScriptLayer& layer) {
    (void) j;
    layer = {};
}

void from_json(nlohmann::json const& j, Script& script) {
    j.at("screens").get_to(script.screens);
    j.at("standbys").get_to(script.standbys);
}

}  // namespace pivid
