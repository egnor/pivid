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
    double begin_x = 0.0, p1_x = 0.0, p2_x = 0.0, end_x = 0.0;
};

// Piecewise-cubic Bezier function in x parameterized on t.
struct BezierSpline {
    std::vector<BezierSegment> segments;  // Distinct & increasing in t.
    bool repeat = false;                  // Repeat after last segment?

    std::optional<double> value(double t) const;
    IntervalSet range(Interval t) const;
};

}  // namespace pivid
