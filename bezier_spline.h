// Cubic Bezier spline description and interpretation

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "interval.h"
#include "xy.h"

namespace pivid {

// A 1-D parametric Bezier segment defined by four control points.
struct BezierSegment {
    Interval t;
    double begin_v = 0.0, p1_v = 0.0, p2_v = 0.0, end_v = 0.0;
};

// Piecewise-cubic Bezier function in x parameterized on t.
struct BezierSpline {
    std::vector<BezierSegment> segments;  // Distinct & increasing in t.
    double repeat = 0.0;                  // If non-0, repeat with this period.

    std::optional<double> value(double t) const;
    IntervalSet range(Interval t) const;
};

BezierSegment constant_segment(Interval t, double v);
BezierSegment linear_segment(Interval t, Interval v);

}  // namespace pivid
