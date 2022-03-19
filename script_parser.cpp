#include "script_parser.h"

#include <exception>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace pivid {

namespace {

Script parse_json(nlohmann::json const& j) {
    (void) j;
    return Script{};
}

}  // anonymous namespace

Script parse_script(std::string text) {
    try {
        return parse_json(nlohmann::json::parse(text));
    } catch (nlohmann::json::parse_error const& je) {
        throw_with_nested(std::runtime_error(je.what()));
    }
}

}  // namespace pivid
