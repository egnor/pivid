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

}  // namespace pivid
