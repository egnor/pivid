// X/Y coordinate and path manipulation.

#pragma once

#include <optional>
#include <utility>
#include <vector>

namespace pivid {

// Convenience struct for coordinate pairs.
template <typename T>
struct XY {
    T x = {}, y = {};

    template <typename U> XY<U> as() const { return XY<U>(x, y); }
    operator bool() const { return x || y; }
    bool operator==(XY const&) const = default;

    XY operator+(XY const other) const { return {x + other.x, y + other.y}; }
    XY operator-(XY const other) const { return {x - other.x, y - other.y}; }
    XY operator-() const { return {-x, -y}; }
    template <typename U> XY operator*(U m) const { return {x * m, y * m}; }
    template <typename U> XY operator/(U d) const { return {x / d, y / d}; }
};

// Piecewise-cubic Bezier curve in x parameterized on t.
struct CubicBezier {
    // One (t, x) pair.
    struct Point { double t, x; };

    // A cubic segment defined by four Bezier control points nondecreasing in t.
    struct Segment { Point begin, p1, p2, end; };

    std::vector<Segment> segments;  // Nonoverlapping and increasing in t.
    double repeat_every = 0.0;      // If nonzero, repeat the curve infinitely.
};

// These return f(t) from a Bezier segment or function, if t is in a segment.
std::optional<double> value_at(CubicBezier::Segment const&, double t);
std::optional<double> value_at(CubicBezier const&, double t);

// Returns min & max f(t) across [t0, t1] intersected with a segment.
std::optional<std::pair<double, double>> minmax_over(
    CubicBezier::Segment const&, double t0, double t1
);

// Returns min & max f(t) for each range-noncontiguous intersection
// of [t0, t1] with a Bezier function.
std::vector<std::pair<double, double>> minmax_over(
    CubicBezier const&, double t0, double t1
);

}  // namespace pivid
