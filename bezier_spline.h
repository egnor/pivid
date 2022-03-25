// Cubic Bezier spline description and interpretation

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "interval_set.h"
#include "xy.h"

namespace pivid {

// A 1-D parametric Bezier segment defined by four control points.
struct BezierSegment {
    Interval<double> t;
    double begin_x = 0.0, p1_x = 0.0, p2_x = 0.0, end_x = 0.0;
};

// Piecewise-cubic Bezier function in x parameterized on t.
struct BezierSpline {
    std::vector<BezierSegment> segments;  // Distinct & increasing in t.
    double repeat = 0.0;                  // Repeat period if nonzero.
};

// These return f(t) from a Bezier segment or function, if t is in a segment.
std::optional<double> bezier_value_at(BezierSpline const&, double t);

// Returns range of f(t) for a Bezier function over an interval in t.
IntervalSet<double> bezier_range_over(BezierSpline const&, Interval<double> t);

}  // namespace pivid
