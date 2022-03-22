// Cubic Bezier spline description and interpretation

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "xy.h"

namespace pivid {

// A 1-D parametric Bezier segment defined by four control points.
struct BezierSegment {
    double begin_t = 0.0, end_t = 0.0;
    double begin_x = 0.0, p1_x = 0.0, p2_x = 0.0, end_x = 0.0;
};

// Piecewise-cubic Bezier curve in x parameterized on t.
struct BezierSpline {
    std::vector<BezierSegment> segments;  // Distinct & increasing in t.
    double repeat = 0.0;                  // Repeat period if nonzero.
};

// These return f(t) from a Bezier segment or function, if t is in a segment.
std::optional<double> bezier_value_at(BezierSpline const&, double t);

// Returns min & max f(t) for each range-noncontiguous intersection
// of [t0, t1] with a Bezier function.
std::vector<std::pair<double, double>> bezier_minmax_over(
    BezierSpline const&, double from_t, double to_t
);

}  // namespace pivid
