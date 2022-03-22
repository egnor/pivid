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
    return t < seg.begin_t;
}

double segment_value_at(BezierSegment const& seg, double t) {
    double const t_len = seg.end_t - seg.begin_t;
    if (t_len < 0) {
        throw std::invalid_argument(
            fmt::format("Bad Bezier: bt={} > et={}", seg.begin_t, seg.end_t)
        );
    }

    if (t < seg.begin_t || t > seg.end_t) {
        throw std::invalid_argument(
            fmt::format("Bad eval: bt={} t={} et={}", seg.begin_t, t, seg.end_t)
        );
    }

    if (t_len <= 0) return 0.5 * (seg.begin_x + seg.end_x);
    double const f = (t - seg.begin_t) / t_len;
    double const nf = 1 - f;
    return (
        nf * nf * nf * seg.begin_x +
        3 * nf * nf * f * seg.p1_x +
        3 * nf * f * f * seg.p2_x +
        f * f * f * seg.end_x
    );
}

void add_minmax_nowrap(
    BezierSpline const& bez, double from_t, double to_t, 
    std::vector<std::pair<double, double>> *out
) {
    auto const& segs = bez.segments;
    auto iter = std::upper_bound(segs.begin(), segs.end(), from_t, lt_begin);
    if (iter != segs.begin()) --iter;

    auto end = std::upper_bound(segs.begin(), segs.end(), to_t, lt_begin);
    for (; iter != end; ++iter) {
        BezierSegment const& s = *iter;
        double const seg_from_t = std::max(s.begin_t, from_t);
        double const seg_to_t = std::min(s.end_t, to_t);
        if (seg_from_t > seg_to_t) continue;

        double const from_x = segment_value_at(s, seg_from_t);
        double const to_x = segment_value_at(s, seg_to_t);
        double min_x = std::min(from_x, to_x);
        double max_x = std::max(from_x, to_x);

        // See https://pomax.github.io/bezierinfo/#extremities
        double const a = 3 * (-s.begin_x + 3 * (s.p1_x - s.p2_x) + s.end_x);
        double const b = 6 * (s.begin_x - 2 * s.p1_x + s.p2_x);
        double const c = 3 * (s.p1_x - s.begin_x);
        double const d = b * b - 4 * a * c;  // Quadratic formula discriminator

        if (d >= 0) {
            double const t_len = s.end_t - s.begin_t;
            double const sqrt_d = std::sqrt(d);

            double const root_a_t = s.begin_t + t_len * (-b - sqrt_d) / (2 * a);
            if (root_a_t >= seg_from_t && root_a_t <= seg_to_t) {
                double const root_a_x = segment_value_at(s, root_a_t);
                min_x = std::min(min_x, root_a_x);
                max_x = std::max(max_x, root_a_x);
            }

            double const root_b_t = s.begin_t + t_len * (-b + sqrt_d) / (2 * a);
            if (root_b_t >= seg_from_t && root_b_t <= seg_to_t) {
                double const root_b_x = segment_value_at(s, root_b_t);
                min_x = std::min(min_x, root_b_x);
                max_x = std::max(max_x, root_b_x);
            }
        }

        if (out->empty()) {
            out->emplace_back(min_x, max_x);
        } else {
            std::pair<double, double> *last = &*out->rbegin();
            if (max_x < last->first || min_x > last->second) {
                out->emplace_back(min_x, max_x);
            } else {
                last->first = std::min(last->first, min_x);
                last->second = std::max(last->second, max_x);
            }
        }
    }
}

}  // anonymous namespace

std::optional<double> bezier_value_at(BezierSpline const& bez, double t) {
    if (bez.segments.empty()) return {};

    double const begin_t = bez.segments.begin()->begin_t;
    if (t < begin_t) return {};
    if (bez.repeat)
        t = std::fmod(t - begin_t, bez.repeat) + begin_t;

    auto const& segs = bez.segments;
    auto const after = std::upper_bound(segs.begin(), segs.end(), t, lt_begin);
    if (after == bez.segments.begin()) return {};
    BezierSegment const& seg = *std::prev(after);
    if (t > seg.end_t) return {};
    if (t < seg.begin_t)
        throw std::logic_error(fmt::format("{} < {}", t, seg.begin_t));
    return segment_value_at(seg, t);
}

std::vector<std::pair<double, double>> bezier_minmax_over(
    BezierSpline const& bez, double from_t, double to_t
) {
    if (bez.segments.empty()) return {};

    double const begin_t = bez.segments.begin()->begin_t;
    from_t = std::max(from_t, begin_t);

    double const len = to_t - from_t;
    if (len < 0) return {};

    std::vector<std::pair<double, double>> out;
    if (!bez.repeat) {
        add_minmax_nowrap(bez, from_t, to_t, &out);
    } else if (len >= bez.repeat) {
        add_minmax_nowrap(bez, begin_t, begin_t + bez.repeat, &out);
    } else {
        double rel_from_t = std::fmod(from_t - begin_t, bez.repeat);
        if (rel_from_t < 0) rel_from_t += bez.repeat;

        double const rel_to_t = std::min(bez.repeat, rel_from_t + len);
        add_minmax_nowrap(bez, begin_t + rel_from_t, begin_t + rel_to_t, &out);

        double const wrap_t = rel_from_t + len - rel_to_t;
        if (wrap_t > 0) add_minmax_nowrap(bez, begin_t, begin_t + wrap_t, &out);
    }

    return out;
}

}  // namespace pivid
