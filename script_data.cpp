#include "script_data.h"

#include <cmath>
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
static void from_json(json const& j, XY<T>& xy) {
    if (j.empty()) {
        xy = {};
    } else if (j.is_object()) {
        j.at("x").get_to(xy.x);
        j.at("y").get_to(xy.y);
    } else {
        CHECK_ARG(j.is_array(), "Bad XY: {}", j.dump());
        j.at(0).get_to(xy.x);
        j.at(1).get_to(xy.y);
    }
}

static double parse_json_time(json const& j) {
    if (j.is_number()) return j;
    CHECK_ARG(j.is_string(), "Bad JSON time: {}", j.dump());
    return parse_realtime(j);
}

static void from_json(json const& j, BezierSegment& seg) {
    if (j.is_number()) {
        seg.t = {0, 1e12};
        seg.begin_v = seg.p1_v = seg.p2_v = seg.end_v = j.get<double>();
        return;
    }

    CHECK_ARG(j.is_object(), "Bad Bezier segment: {}", j.dump());
    auto const t_j = j.value("t", json{});
    if (t_j.empty()) {
        seg.t = {0.0, j.value("len", 1e12)};
    } else if (t_j.is_number() || (t_j.is_array() && t_j.size() == 1)) {
        seg.t.begin = parse_json_time(t_j.is_array() ? t_j.at(0) : t_j);
        seg.t.end = seg.t.begin + j.value("len", 1e12 - seg.t.begin);
    } else {
        CHECK_ARG(t_j.is_array(), "Bad Bezier \"t\": {}", j.dump());
        CHECK_ARG(t_j.size() == 2, "Bad Bezier \"t\" length: {}", j.dump());
        seg.t = {parse_json_time(t_j.at(0)), parse_json_time(t_j.at(1))};
    }

    double const dt = seg.t.end - seg.t.begin;
    CHECK_ARG(dt >= 0.0, "Bad Bezier segment times: {}", j.dump());

    auto const v_j = j.value("v", json{});
    if (v_j.empty()) {
        seg.begin_v = 0.0;
        seg.end_v = seg.begin_v + j.value("rate", 0.0) * dt;
        seg.p1_v = seg.begin_v + (seg.end_v - seg.begin_v) / 3.0;
        seg.p2_v = seg.end_v - (seg.end_v - seg.begin_v) / 3.0;
    } else if (v_j.is_number() || (v_j.is_array() && v_j.size() == 1)) {
        (v_j.is_array() ? v_j.at(0) : v_j).get_to(seg.begin_v);
        seg.end_v = seg.begin_v + j.value("rate", 0.0) * dt;
        seg.p1_v = seg.begin_v + (seg.end_v - seg.begin_v) / 3.0;
        seg.p2_v = seg.end_v - (seg.end_v - seg.begin_v) / 3.0;
    } else if (v_j.is_array() && v_j.size() == 2) {
        v_j.at(0).get_to(seg.begin_v);
        v_j.at(1).get_to(seg.end_v);

        auto const rate_j = j.value("rate", json{});
        if (rate_j.empty()) {
            seg.p1_v = seg.begin_v + (seg.end_v - seg.begin_v) / 3.0;
            seg.p2_v = seg.end_v - (seg.end_v - seg.begin_v) / 3.0;
        } else {
            CHECK_ARG(rate_j.size() == 2, "Bad Bezier \"rate\": {}", j.dump());
            seg.p1_v = seg.begin_v + rate_j.at(0).get<double>() * dt / 3.0;
            seg.p2_v = seg.end_v - rate_j.at(1).get<double>() * dt / 3.0;
        }
    } else if (v_j.is_array() && v_j.size() == 4) {
        CHECK_ARG(v_j.is_array(), "Bad Bezier \"x\": {}", j.dump());
        CHECK_ARG(v_j.size() == 4, "Bad Bezier \"x\" length: {}", j.dump());
        v_j.at(0).get_to(seg.begin_v);
        v_j.at(1).get_to(seg.p1_v);
        v_j.at(2).get_to(seg.p2_v);
        v_j.at(3).get_to(seg.end_v);
        CHECK_ARG(!j.count("rate"), "Redundant Bezier \"rate\": {}", j.dump());
    }
}

static void from_json(json const& j, BezierSpline& bezier) {
    if (j.is_object() && j.count("segments")) {
        j.at("segments").get_to(bezier.segments);
    } else if (j.is_array() && !j.at(0).is_number()) {
        j.get_to(bezier.segments);
    } else if (!j.empty()) {
        bezier.segments.resize(1);
        j.get_to(bezier.segments[0]);
    }

    for (size_t si = 0; si + 1 < bezier.segments.size(); ++si) {
        CHECK_ARG(
             bezier.segments[si].t.end <= bezier.segments[si + 1].t.begin,
             "Bad Bezier time sequence: {}", j.dump()
        );
    }

    if (j.is_object()) {
        auto const rep_j = j.value("repeat", json{});
        if (rep_j.is_number()) {
            bezier.repeat = rep_j.get<double>();
        } else if (rep_j.is_boolean()) {
            auto const& segs = bezier.segments;
            if (rep_j.get<bool>() && !bezier.segments.empty())
                bezier.repeat = segs.rbegin()->t.end - segs.begin()->t.begin;
        } else {
            CHECK_ARG(rep_j.is_null(), "Bad Bezier \"repeat\": {}", j.dump());
        }
    }

    CHECK_ARG(bezier.repeat >= 0, "Bad Bezier repeat period: {}", j.dump());
}

static void from_json(json const& j, ScriptMedia& m) {
    CHECK_ARG(j.count("file"), "No \"file\" in JSON media: {}", j.dump());
    j.at("file").get_to(m.file);
    j.value("play", json(0)).get_to(m.play);
    m.playtime_buffer = j.value("playtime_buffer", m.playtime_buffer);
    CHECK_ARG(m.playtime_buffer >= 0.0, "Bad playtime_buffer: {}", j.dump());
    m.mediatime_buffer = j.value("mediatime_buffer", m.mediatime_buffer);
}

static void from_json(json const& j, ScriptLayer& layer) {
    CHECK_ARG(j.is_object(), "Bad JSON layer: {}", j.dump());
    CHECK_ARG(j.count("media"), "No \"media\" in JSON layer: {}", j.dump());
    j.at("media").get_to(layer.media);
    j.value("from_xy", json{}).get_to(layer.from_xy);
    j.value("from_size", json{}).get_to(layer.from_size);
    j.value("to_xy", json{}).get_to(layer.to_xy);
    j.value("to_size", json{}).get_to(layer.to_size);
    j.value("opacity", json{}).get_to(layer.opacity);
}

static void from_json(json const& j, ScriptScreen& screen) {
    CHECK_ARG(j.is_object(), "Bad JSON screen: {}", j.dump());
    j.value("display_mode", json{}).get_to(screen.display_mode);
    screen.display_hz = j.value("display_hz", screen.display_hz);
    screen.update_hz = j.value("update_hz", screen.update_hz);
    CHECK_ARG(screen.update_hz >= 0.0, "Bad update_hz: {}", j.dump());
    j.value("layers", json::array()).get_to(screen.layers);
}

static void from_json(json const& j, Script& s) {
    s = {};
    CHECK_ARG(j.is_object(), "Bad JSON script: {}", j.dump());
    j.value("screens", json::object()).get_to(s.screens);
    j.value("standbys", json::array()).get_to(s.standbys);
    if (j.count("zero_time"))
        s.zero_time = j.at("zero_time").get<double>();
    s.main_loop_hz = j.value("main_loop_hz", s.main_loop_hz);
    s.main_buffer_time = j.value("main_buffer_time", s.main_buffer_time);
    CHECK_ARG(s.main_loop_hz > 0.0, "Bad main_loop_hz: {}", j.dump());
}

Script parse_script(std::string_view text) {
    try {
        return nlohmann::json::parse(text).get<Script>();
    } catch (nlohmann::json::exception const& e) {
        std::throw_with_nested(std::invalid_argument(e.what()));
    }
}

}  // namespace pivid
