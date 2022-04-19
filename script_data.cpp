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
        seg = constant_segment({0, 1e12}, j.get<double>());
        return;
    }

    CHECK_ARG(j.is_object(), "Bad Bezier segment: {}", j.dump());
    auto const t_j = j.value("t", json());
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

    auto const v_j = j.value("v", json());
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

        auto const rate_j = j.value("rate", json());
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
        auto const rep_j = j.value("repeat", json());
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
    auto const preload_j = j.value("preload", json());
    if (preload_j.is_number()) {
        auto* preload = &m.preload.emplace_back();
        from_json(json(0.0), preload->begin);
        from_json(preload_j, preload->end);
    } else if (
        preload_j.is_array() && preload_j.size() == 2 &&
        !preload_j.at(0).is_array()
    ) {
        auto* preload = &m.preload.emplace_back();
        from_json(preload_j.at(0), preload->begin);
        from_json(preload_j.at(1), preload->end);
    } else {
        CHECK_ARG(preload_j.is_array(), "Bad JSON preload: {}", j.dump());
        for (auto const& item_j : preload_j) {
            CHECK_ARG(
                item_j.is_array() && item_j.size() == 2,
                "Bad JSON preload range: {}", j.dump()
            );
            auto* preload = &m.preload.emplace_back();
            from_json(item_j.at(0), preload->begin);
            from_json(item_j.at(1), preload->end);
        }
    }

    m.seek_scan_time = j.value("seek_scan_time", m.seek_scan_time);
    CHECK_ARG(m.seek_scan_time >= 0.0, "Bad seek_scan_time: {}", j.dump());

    m.decoder_idle_time = j.value("decoder_idle_time", m.decoder_idle_time);
    CHECK_ARG(
        m.decoder_idle_time >= 0.0, "Bad decoder_idle_time: {}", j.dump()
    );
}

static void from_json(json const& j, ScriptLayer& layer) {
    CHECK_ARG(j.is_object(), "Bad JSON layer: {}", j.dump());
    layer.media = j.value("media", "");
    CHECK_ARG(!layer.media.empty(), "No \"media\" in JSON layer: {}", j.dump());
    j.value("play", json(0)).get_to(layer.play);
    j.value("buffer", json(layer.buffer)).get_to(layer.buffer);
    j.value("from_xy", json()).get_to(layer.from_xy);
    j.value("from_size", json()).get_to(layer.from_size);
    j.value("to_xy", json()).get_to(layer.to_xy);
    j.value("to_size", json()).get_to(layer.to_size);
    j.value("opacity", json()).get_to(layer.opacity);
}

static void from_json(json const& j, ScriptScreen& screen) {
    CHECK_ARG(j.is_object(), "Bad JSON screen: {}", j.dump());
    j.value("display_mode", json()).get_to(screen.display_mode);
    screen.display_hz = j.value("display_hz", screen.display_hz);
    screen.update_hz = j.value("update_hz", screen.update_hz);
    CHECK_ARG(screen.update_hz >= 0.0, "Bad update_hz: {}", j.dump());
    j.value("layers", json::array()).get_to(screen.layers);
}

Script parse_script(std::string_view text, double default_zero_time) {
    try {
        Script s = {};
        auto const j = nlohmann::json::parse(text);
        CHECK_ARG(j.is_object(), "Bad JSON script: {}", j.dump());
        j.value("media", json::object()).get_to(s.media);
        j.value("screens", json::object()).get_to(s.screens);
        s.zero_time = j.value("zero_time", default_zero_time);
        s.main_loop_hz = j.value("main_loop_hz", s.main_loop_hz);
        s.main_buffer_time = j.value("main_buffer_time", s.main_buffer_time);
        CHECK_ARG(s.main_loop_hz > 0.0, "Bad main_loop_hz: {}", j.dump());
        return s;
    } catch (nlohmann::json::exception const& e) {
        std::throw_with_nested(std::invalid_argument(e.what()));
    }
}

}  // namespace pivid
