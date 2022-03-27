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

void add_minmax_nowrap(
    BezierSpline const& bez, double t_begin, double t_end, 
    IntervalSet<double>* out
) {
    auto const& segs = bez.segments;
    auto iter = std::upper_bound(segs.begin(), segs.end(), t_begin, lt_begin);
    if (iter != segs.begin()) --iter;

    auto end = std::upper_bound(segs.begin(), segs.end(), t_end, lt_begin);
    for (; iter != end; ++iter) {
        BezierSegment const& s = *iter;
        double const seg_t_begin = std::max(s.t.begin, t_begin);
        double const seg_t_end = std::min(s.t.end, t_end);
        if (seg_t_begin > seg_t_end) continue;

        double const begin_x = segment_value_at(s, seg_t_begin);
        double const end_x = segment_value_at(s, seg_t_end);
        double min_x = std::min(begin_x, end_x);
        double max_x = std::max(begin_x, end_x);

        // See https://pomax.github.io/bezierinfo/#extremities
        double const a = 3 * (-s.begin_x + 3 * (s.p1_x - s.p2_x) + s.end_x);
        double const b = 6 * (s.begin_x - 2 * s.p1_x + s.p2_x);
        double const c = 3 * (s.p1_x - s.begin_x);
        double const d = b * b - 4 * a * c;  // Quadratic formula discriminator

        if (d >= 0) {
            double const t_len = s.t.end - s.t.begin;
            double const sqrt_d = std::sqrt(d);

            double const root_a_t = s.t.begin + t_len * (-b - sqrt_d) / (2 * a);
            if (root_a_t >= seg_t_begin && root_a_t <= seg_t_end) {
                double const root_a_x = segment_value_at(s, root_a_t);
                min_x = std::min(min_x, root_a_x);
                max_x = std::max(max_x, root_a_x);
            }

            double const root_b_t = s.t.begin + t_len * (-b + sqrt_d) / (2 * a);
            if (root_b_t >= seg_t_begin && root_b_t <= seg_t_end) {
                double const root_b_x = segment_value_at(s, root_b_t);
                min_x = std::min(min_x, root_b_x);
                max_x = std::max(max_x, root_b_x);
            }
        }

        out->insert({min_x, max_x});
    }
}

}  // anonymous namespace

std::optional<double> bezier_value_at(BezierSpline const& bez, double t) {
    if (bez.segments.empty()) return {};

    double const bez_begin = bez.segments.begin()->t.begin;
    if (t < bez_begin) return {};
    if (bez.repeat)
        t = std::fmod(t - bez_begin, bez.repeat) + bez_begin;

    auto const& segs = bez.segments;
    auto const after = std::upper_bound(segs.begin(), segs.end(), t, lt_begin);
    if (after == bez.segments.begin()) return {};
    BezierSegment const& seg = *std::prev(after);
    ASSERT(t >= seg.t.begin);
    if (t > seg.t.end) return {};
    return segment_value_at(seg, t);
}

IntervalSet<double> bezier_range_over(
    BezierSpline const& bez, Interval<double> t
) {
    if (bez.segments.empty()) return {};

    double const bez_begin = bez.segments.begin()->t.begin;
    t.begin = std::max(t.begin, bez_begin);
    double const len = t.end - t.begin;
    if (len < 0) return {};

    IntervalSet<double> out;
    if (!bez.repeat) {
        add_minmax_nowrap(bez, t.begin, t.end, &out);
    } else if (len >= bez.repeat) {
        add_minmax_nowrap(bez, bez_begin, bez_begin + bez.repeat, &out);
    } else {
        double r_begin = std::fmod(t.begin - bez_begin, bez.repeat);
        if (r_begin < 0) r_begin += bez.repeat;

        double const r_end = std::min(bez.repeat, r_begin + len);
        add_minmax_nowrap(bez, bez_begin + r_begin, bez_begin + r_end, &out);

        double const wrap_t = r_begin + len - r_end;
        if (wrap_t > 0)
            add_minmax_nowrap(bez, bez_begin, bez_begin + wrap_t, &out);
    }

    return out;
}

}  // namespace pivid
