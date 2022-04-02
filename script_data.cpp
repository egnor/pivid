#include "script_data.h"

#include <chrono>
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

void from_json(json const& j, BezierSpline& bezier) {
    if (j.is_object() && j.count("segments")) {
        j.at("segments").get_to(bezier.segments);
        bezier.repeat = j.value("repeat", false);
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
    j.value("mode", json{}).get_to(screen.mode);
    screen.mode_hz = j.value("mode_hz", screen.mode_hz);
    j.value("layers", json::array()).get_to(screen.layers);
}

void from_json(json const& j, Script& script) {
    script = {};
    CHECK_ARG(j.is_object(), "Bad JSON script: {}", j.dump());
    j.value("screens", json::object()).get_to(script.screens);
    j.value("standbys", json::array()).get_to(script.standbys);
    script.run_relative = j.value("run_relative", false);
}

Script make_script_absolute(Script script, double run_start) {
    if (!script.run_relative) return script;

    auto const fix_time = [run_start](double* t) {
        if (std::abs(*t) < 1e12) *t += run_start;
    };

    auto const fix_bezier = [fix_time](BezierSpline* bezier) {
        for (auto& segment : bezier->segments) {
            fix_time(&segment.t.begin);
            fix_time(&segment.t.end);
        }
    };

    auto const fix_bezier_xy = [fix_bezier](XY<BezierSpline>* xy) {
        fix_bezier(&xy->x);
        fix_bezier(&xy->y);
    };

    for (auto& screen : script.screens) {
        for (auto& layer : screen.second.layers) {
            fix_bezier(&layer.media.play);
            fix_bezier_xy(&layer.from_xy);
            fix_bezier_xy(&layer.from_size);
            fix_bezier_xy(&layer.to_xy);
            fix_bezier_xy(&layer.to_size);
            fix_bezier(&layer.opacity);
        }
    }

    for (auto& standby : script.standbys)
        fix_bezier(&standby.play);

    script.run_relative = false;
    return script;
}

}  // namespace pivid
