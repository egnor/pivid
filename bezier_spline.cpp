#include "bezier_spline.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

#include <fmt/core.h>

#include "logging_policy.h"

namespace pivid {

namespace {

bool lt_begin(double const t, BezierSegment const& seg) {
    return t < seg.t.begin;
}

double segment_value_at(BezierSegment const& seg, double t) {
    double const t_len = seg.t.end - seg.t.begin;
    CHECK_ARG(
        t_len >= 0,
        "Bad Bezier: bt={} > et={}", seg.t.begin, seg.t.end
    );
    CHECK_ARG(
        seg.t.begin <= t && t <= seg.t.end,
        "Bad eval: bt={} t={} et={}", seg.t.begin, t, seg.t.end
    );

    if (t_len <= 0) return 0.5 * (seg.begin_x + seg.end_x);
    double const f = (t - seg.t.begin) / t_len;
    double const nf = 1 - f;
    return seg.begin_x + (
        3 * nf * nf * f * (seg.p1_x - seg.begin_x) +
        3 * nf * f * f * (seg.p2_x - seg.begin_x) +
        f * f * f * (seg.end_x - seg.begin_x)
    );
}

Interval<double> segment_range(
    BezierSegment const& seg, Interval<double> t
) {
    t.begin = std::max(seg.t.begin, t.begin);
    t.end = std::min(seg.t.end, t.end);
    if (t.empty()) return {};

    double const begin_x = segment_value_at(seg, t.begin);
    double const end_x = segment_value_at(seg, t.end);
    double min_x = std::min(begin_x, end_x);
    double max_x = std::max(begin_x, end_x);

    // See https://pomax.github.io/bezierinfo/#extremities
    double const a = 3 * (-seg.begin_x + 3 * (seg.p1_x - seg.p2_x) + seg.end_x);
    double const b = 6 * (seg.begin_x - 2 * seg.p1_x + seg.p2_x);
    double const c = 3 * (seg.p1_x - seg.begin_x);
    double const d = b * b - 4 * a * c;  // Quadratic formula discriminator

    if (d >= 0) {
        double const t_len = seg.t.end - seg.t.begin;
        double const sqrt_d = std::sqrt(d);

        double const root_a_t = seg.t.begin + t_len * (-b - sqrt_d) / (2 * a);
        if (root_a_t >= t.begin && root_a_t <= t.end) {
            double const root_a_x = segment_value_at(seg, root_a_t);
            min_x = std::min(min_x, root_a_x);
            max_x = std::max(max_x, root_a_x);
        }

        double const root_b_t = seg.t.begin + t_len * (-b + sqrt_d) / (2 * a);
        if (root_b_t >= t.begin && root_b_t <= t.end) {
            double const root_b_x = segment_value_at(seg, root_b_t);
            min_x = std::min(min_x, root_b_x);
            max_x = std::max(max_x, root_b_x);
        }
    }

    return {min_x, max_x};
}

void add_range_nowrap(
    BezierSpline const& bez, Interval<double> t, IntervalSet<double>* out
) {
    auto const& segs = bez.segments;
    auto iter = std::upper_bound(segs.begin(), segs.end(), t.begin, lt_begin);
    if (iter != segs.begin()) --iter;

    auto end = std::upper_bound(segs.begin(), segs.end(), t.end, lt_begin);
    for (; iter != end; ++iter)
        out->insert(segment_range(*iter, t));
}

}  // anonymous namespace

std::optional<double> BezierSpline::value(double t) const {
    if (segments.empty()) return {};

    if (repeat) {
        Interval const bz{segments.begin()->t.begin, segments.rbegin()->t.end};
        t = std::fmod(t - bz.begin, bz.end - bz.begin) + bz.begin;
    }

    auto const& segs = segments;
    auto const after = std::upper_bound(segs.begin(), segs.end(), t, lt_begin);
    if (after == segments.begin()) return {};
    BezierSegment const& seg = *std::prev(after);
    ASSERT(seg.t.begin <= t);
    return t <= seg.t.end ? segment_value_at(seg, t) : std::optional<double>{};
}

IntervalSet<double> BezierSpline::range(Interval<double> t) const {
    if (segments.empty() || t.empty()) return {};

    IntervalSet<double> out;
    if (!repeat) {
        add_range_nowrap(*this, t, &out);
        return out;
    }

    Interval const bz{segments.begin()->t.begin, segments.rbegin()->t.end};
    t.begin = std::max(t.begin, bz.begin);
    if (t.end - t.begin > bz.end - bz.begin) {
        add_range_nowrap(*this, t, &out);
        return out;
    }

    Interval<double> wrap;
    wrap.begin = std::fmod(t.begin - bz.begin, bz.end - bz.begin) + bz.begin;
    wrap.end = (t.end - t.begin) + wrap.end;
    if (wrap.end <= bz.end) {
        add_range_nowrap(*this, wrap, &out);
        return out;
    }

    add_range_nowrap(*this, {wrap.begin, bz.end}, &out);
    add_range_nowrap(*this, {bz.begin, (wrap.end - bz.end) + bz.begin}, &out);
    return out;
}

}  // namespace pivid