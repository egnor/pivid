#include "script_data.h"

#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "unix_system.h"

using json = nlohmann::json;

namespace pivid {

template <typename T>
void from_json(json const& j, XY<T>& xy) {
    if (j.empty()) {
        xy = {};
    } else {
        j.at(0).get_to(xy.x);
        j.at(1).get_to(xy.y);
    }
}

static double parse_json_time(json const& j) {
    if (j.is_number()) {
        return j;
    } else if (j.is_string()) {
        return parse_system_time(j).time_since_epoch().count();
    } else {
        throw std::runtime_error("Bad time: " + j.dump());
    }
}

void from_json(json const& j, BezierSegment& seg) {
    if (j.is_number()) {
        seg.t = {0.0, 1e12};
        seg.begin_x = seg.p1_x = seg.p2_x = seg.end_x = j.get<double>();
        return;
    }

    if (!j.is_object()) throw std::runtime_error("Bad segment: " + j.dump());

    auto const t_j = j.value("t", json{});
    if (t_j.empty()) {
        seg.t = {0.0, j.value("len", 1e12)};
    } else if (t_j.is_number() || (t_j.is_array() && t_j.size() == 1)) {
        seg.t.begin = parse_json_time(t_j.is_array() ? t_j.at(0) : t_j);
        seg.t.end = seg.t.begin + j.value("len", 1e12);
    } else if (t_j.is_array() && t_j.size() == 2) {
        seg.t = {parse_json_time(t_j.at(0)), parse_json_time(t_j.at(1))};
    } else {
        throw std::runtime_error("Bad segment \"t\": " + j.dump());
    }

    double const dt = seg.t.end - seg.t.begin;
    if (dt < 0.0)
        throw std::runtime_error("Bad segment times: " + j.dump());

    auto const x_j = j.value("x", json{});
    if (x_j.empty()) {
        seg.begin_x = 0.0;
        seg.end_x = seg.begin_x + j.value("rate", 0.0) * dt;
        seg.p1_x = seg.begin_x + (seg.end_x - seg.begin_x) / 3.0;
        seg.p2_x = seg.end_x - (seg.end_x - seg.begin_x) / 3.0;
    } else if (x_j.is_number() || (x_j.is_array() && x_j.size() == 1)) {
        (x_j.is_array() ? x_j.at(0) : x_j).get_to(seg.begin_x);
        seg.end_x = seg.begin_x + j.value("rate", 0.0) * dt;
        seg.p1_x = seg.begin_x + (seg.end_x - seg.begin_x) / 3.0;
        seg.p2_x = seg.end_x - (seg.end_x - seg.begin_x) / 3.0;
    } else if (x_j.is_array() && x_j.size() == 2) {
        x_j.at(0).get_to(seg.begin_x);
        x_j.at(1).get_to(seg.end_x);

        auto const rate_j = j.value("rate", json{});
        if (rate_j.empty()) {
            seg.p1_x = seg.begin_x + (seg.end_x - seg.begin_x) / 3.0;
            seg.p2_x = seg.end_x - (seg.end_x - seg.begin_x) / 3.0;
        } else if (rate_j.size() == 2) {
            seg.p1_x = seg.begin_x + rate_j.at(0).get<double>() * dt / 3.0;
            seg.p2_x = seg.end_x - rate_j.at(1).get<double>() * dt / 3.0;
        } else {
            throw std::runtime_error("Bad segment \"rate\": " + j.dump());
        }
    } else if (x_j.is_array() && x_j.size() == 4) {
        x_j.at(0).get_to(seg.begin_x);
        x_j.at(1).get_to(seg.p1_x);
        x_j.at(2).get_to(seg.p2_x);
        x_j.at(3).get_to(seg.end_x);
        if (j.count("rate"))
            throw std::runtime_error("Redundant \"rate\": " + j.dump());
    } else {
        throw std::runtime_error("Bad segment \"x\": " + j.dump());
    }
}

void from_json(json const& j, BezierSpline& bezier) {
    if (j.is_object() && j.count("segments")) {
        j.at("segments").get_to(bezier.segments);
        bezier.repeat = j.value("repeat", 0.0);
    } else if (j.is_array() && !j.at(0).is_number()) {
        j.get_to(bezier.segments);
    } else if (!j.empty()) {
        bezier.segments.resize(1);
        j.get_to(bezier.segments[0]);
    }
}

void from_json(json const& j, ScriptMedia& media) {
    if (!j.count("file")) throw std::runtime_error("No file: " + j.dump());
    j.at("file").get_to(media.file);
    j.value("play", json{}).get_to(media.play);
    media.buffer = j.value("buffer", media.buffer);
}

void from_json(json const& j, ScriptLayer& layer) {
    if (!j.is_object()) throw std::runtime_error("Bad layer: " + j.dump());
    if (!j.count("media")) throw std::runtime_error("No media: " + j.dump());
    j.at("media").get_to(layer.media);
    j.value("from_xy", json{}).get_to(layer.from_xy);
    j.value("from_size", json{}).get_to(layer.from_size);
    j.value("to_xy", json{}).get_to(layer.to_xy);
    j.value("to_size", json{}).get_to(layer.to_size);
    j.value("opacity", json{}).get_to(layer.opacity);
}

void from_json(json const& j, ScriptScreen& screen) {
    if (!j.is_object()) throw std::runtime_error("Bad screen: " + j.dump());
    screen.mode = j.value("mode", "");
    j.value("layers", json::array()).get_to(screen.layers);
}

void from_json(json const& j, Script& script) {
    script = {};
    if (!j.is_object()) throw std::runtime_error("Bad script: " + j.dump());
    j.value("screens", json::object()).get_to(script.screens);
    j.value("standbys", json::array()).get_to(script.standbys);
}

}  // namespace pivid
