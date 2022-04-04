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

    if (t_len <= 0) return 0.5 * (seg.begin_v + seg.end_v);
    double const f = (t - seg.t.begin) / t_len;
    double const nf = 1 - f;
    return seg.begin_v + (
        3 * nf * nf * f * (seg.p1_v - seg.begin_v) +
        3 * nf * f * f * (seg.p2_v - seg.begin_v) +
        f * f * f * (seg.end_v - seg.begin_v)
    );
}

Interval segment_range(BezierSegment const& seg, Interval t) {
    t.begin = std::max(seg.t.begin, t.begin);
    t.end = std::min(seg.t.end, t.end);
    if (t.empty()) return {};

    double const begin_v = segment_value_at(seg, t.begin);
    double const end_v = segment_value_at(seg, t.end);
    double min_v = std::min(begin_v, end_v);
    double max_v = std::max(begin_v, end_v);

    // See https://pomax.github.io/bezierinfo/#extremities
    double const a = 3 * (-seg.begin_v + 3 * (seg.p1_v - seg.p2_v) + seg.end_v);
    double const b = 6 * (seg.begin_v - 2 * seg.p1_v + seg.p2_v);
    double const c = 3 * (seg.p1_v - seg.begin_v);
    double const d = b * b - 4 * a * c;  // Quadratic formula discriminator

    if (d >= 0) {
        double const t_len = seg.t.end - seg.t.begin;
        double const sqrt_d = std::sqrt(d);

        double const root_a_t = seg.t.begin + t_len * (-b - sqrt_d) / (2 * a);
        if (root_a_t >= t.begin && root_a_t <= t.end) {
            double const root_a_v = segment_value_at(seg, root_a_t);
            min_v = std::min(min_v, root_a_v);
            max_v = std::max(max_v, root_a_v);
        }

        double const root_b_t = seg.t.begin + t_len * (-b + sqrt_d) / (2 * a);
        if (root_b_t >= t.begin && root_b_t <= t.end) {
            double const root_b_v = segment_value_at(seg, root_b_t);
            min_v = std::min(min_v, root_b_v);
            max_v = std::max(max_v, root_b_v);
        }
    }

    ASSERT(max_v >= min_v);
    if (max_v > min_v) return {min_v, max_v};
    return {min_v, std::nextafter(max_v, std::numeric_limits<double>::max())};
}

void add_range_nowrap(BezierSpline const& bez, Interval t, IntervalSet* out) {
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

IntervalSet BezierSpline::range(Interval t) const {
    if (segments.empty() || t.empty()) return {};

    IntervalSet out;
    if (!repeat) {
        add_range_nowrap(*this, t, &out);
        return out;
    }

    Interval const bz{segments.begin()->t.begin, segments.rbegin()->t.end};
    t.begin = std::max(t.begin, bz.begin);
    if (t.end - t.begin > bz.end - bz.begin) {
        add_range_nowrap(*this, bz, &out);
        return out;
    }

    Interval wrap;
    wrap.begin = bz.begin + std::fmod(t.begin - bz.begin, bz.end - bz.begin);
    wrap.end = wrap.begin + (t.end - t.begin);
    if (wrap.end <= bz.end) {
        add_range_nowrap(*this, wrap, &out);
        return out;
    }

    add_range_nowrap(*this, {wrap.begin, bz.end}, &out);
    add_range_nowrap(*this, {bz.begin, bz.begin + (wrap.end - bz.end)}, &out);
    return out;
}

BezierSegment constant_segment(Interval t, double v) {
    return {.t = t, .begin_v = v, .p1_v = v, .p2_v = v, .end_v = v};
}

BezierSegment linear_segment(Interval t, Interval v) {
    BezierSegment seg;
    seg.t = t;
    seg.begin_v = v.begin;
    seg.p1_v = v.begin + (v.end - v.begin) / 3.0;
    seg.p2_v = v.end - (v.end - v.begin) / 3.0;
    seg.end_v = v.end;
    return seg;
}

}  // namespace pivid
