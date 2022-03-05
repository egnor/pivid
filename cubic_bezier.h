// Cubic Bezier spline description and interpretation

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "xy.h"

namespace pivid {

// Piecewise-cubic Bezier curve in x parameterized on t.
struct CubicBezier {
    // A 1-D parametric Bezier segment defined by four control points.
    struct Segment { double begin_t, end_t, begin_x, p1_x, p2_x, end_x; };

    std::vector<Segment> segments;  // Nonoverlapping and increasing in t.
    double repeat_every = 0.0;      // If nonzero, repeat the curve infinitely.
};

// These return f(t) from a Bezier segment or function, if t is in a segment.
std::optional<double> bezier_value_at(CubicBezier const&, double t);

// Returns min & max f(t) for each range-noncontiguous intersection
// of [t0, t1] with a Bezier function.
std::vector<std::pair<double, double>> bezier_minmax_over(
    CubicBezier const&, double from_t, double to_t
);

}  // namespace pivid
