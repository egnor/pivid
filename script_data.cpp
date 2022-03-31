#include "script_data.h"

#include <chrono>
#include <exception>
#include <limits>
#include <stdexcept>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include "logging_policy.h"
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
    if (j.is_number()) return j;
    CHECK_ARG(j.is_string(), "Bad JSON time: {}", j.dump());
    return parse_time(j);
}

void from_json(json const& j, BezierSegment& seg) {
    if (j.is_number()) {
        seg.t = {0.0, 1e12};
        seg.begin_x = seg.p1_x = seg.p2_x = seg.end_x = j.get<double>();
        return;
    }

    CHECK_ARG(j.is_object(), "Bad Bezier segment: {}", j.dump());
    auto const t_j = j.value("t", json{});
    if (t_j.empty()) {
        seg.t = {0.0, j.value("len", 1e12)};
    } else if (t_j.is_number() || (t_j.is_array() && t_j.size() == 1)) {
        seg.t.begin = parse_json_time(t_j.is_array() ? t_j.at(0) : t_j);
        seg.t.end = seg.t.begin + j.value("len", 1e12);
    } else {
        CHECK_ARG(t_j.is_array(), "Bad Bezier \"t\": {}", j.dump());
        CHECK_ARG(t_j.size() == 2, "Bad Bezier \"t\" length: {}", j.dump());
        seg.t = {parse_json_time(t_j.at(0)), parse_json_time(t_j.at(1))};
    }

    double const dt = seg.t.end - seg.t.begin;
    CHECK_ARG(dt >= 0.0, "Bad Bezier segment times: {}", j.dump());

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
        } else {
            CHECK_ARG(rate_j.size() == 2, "Bad Bezier \"rate\": {}", j.dump());
            seg.p1_x = seg.begin_x + rate_j.at(0).get<double>() * dt / 3.0;
            seg.p2_x = seg.end_x - rate_j.at(1).get<double>() * dt / 3.0;
        }
    } else if (x_j.is_array() && x_j.size() == 4) {
        CHECK_ARG(x_j.is_array(), "Bad Bezier \"x\": {}", j.dump());
        CHECK_ARG(x_j.size() == 4, "Bad Bezier \"x\" length: {}", j.dump());
        x_j.at(0).get_to(seg.begin_x);
        x_j.at(1).get_to(seg.p1_x);
        x_j.at(2).get_to(seg.p2_x);
        x_j.at(3).get_to(seg.end_x);
        CHECK_ARG(!j.count("rate"), "Redundant Bezier \"rate\": {}", j.dump());
    }
}

void from_json(json const& j, BezierSpline& bezier) {
    if (j.is_object() && j.count("segments")) {
        j.at("segments").get_to(bezier.segments);
        bezier.repeat = j.value("repeat", false);
    } else if (j.is_array() && !j.at(0).is_number()) {
        j.get_to(bezier.segments);
        for (size_t si = 0; si < bezier.segments.size() - 1; ++si) {
            CHECK_ARG(
                 bezier.segments[si].t.end <= bezier.segments[si + 1].t.begin,
                 "Bad Bezier time sequence: {}", j.dump()
            );
        }
    } else if (!j.empty()) {
        bezier.segments.resize(1);
        j.get_to(bezier.segments[0]);
    }
}

void from_json(json const& j, ScriptMedia& media) {
    CHECK_ARG(j.count("file"), "No JSON media file: {}", j.dump());
    j.at("file").get_to(media.file);
    j.value("play", json{}).get_to(media.play);
    media.buffer = j.value("buffer", media.buffer);
}

void from_json(json const& j, ScriptLayer& layer) {
    CHECK_ARG(j.is_object(), "Bad JSON layer: {}", j.dump());
    CHECK_ARG(j.count("media"), "No JSON layer media: {}", j.dump());
    j.at("media").get_to(layer.media);
    j.value("from_xy", json{}).get_to(layer.from_xy);
    j.value("from_size", json{}).get_to(layer.from_size);
    j.value("to_xy", json{}).get_to(layer.to_xy);
    j.value("to_size", json{}).get_to(layer.to_size);
    j.value("opacity", json{}).get_to(layer.opacity);
}

void from_json(json const& j, ScriptScreen& screen) {
    CHECK_ARG(j.is_object(), "Bad JSON screen: {}", j.dump());
    screen.mode = j.value("mode", "");
    j.value("layers", json::array()).get_to(screen.layers);
}

void from_json(json const& j, Script& script) {
    script = {};
    CHECK_ARG(j.is_object(), "Bad JSON script: {}", j.dump());
    j.value("screens", json::object()).get_to(script.screens);
    j.value("standbys", json::array()).get_to(script.standbys);
}

}  // namespace pivid
