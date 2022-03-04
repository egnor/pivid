#include "cubic_bezier.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace pivid {

namespace {

bool operator<(double const t, CubicBezier::Segment const& seg) {
    return t < s.begin.t;
}

double segment_value_at(CubicBezier::Segment const& seg, double t) {
    const double len = seg.end.t - seg.begin.t;
    if (len < 0) {
        throw std::invalid_argument(
            "Bad Bezier segment order: {} > {}", seg.begin.t, seg.end.t
        );
    }

    if (len <= 0) return 0.5 * (seg.begin.x + seg.end.x);
    double const f = (t - seg.begin.t) / len;
    double const nf = 1 - f;
    return (
        nf * nf * nf * seg.begin.x +
        3 * nf * nf * f * seg.p1.x +
        3 * nf * f * f * seg.p2.x +
        f * f * f * seg.end.x
    );
}

void add_minmax_nowrap(
    CubicBezier const& bez, double t0, double t1, 
    std::vector<std::pair<double, double>> *out
) {
    auto const& segs = bez.segments;
    auto iter = std::upper_bound(segs.begin(), segs.end(), t0);
    if (iter != segs.begin()) --iter;

    auto end = std::upper_bound(segs.begin(), segs.end(), t1);
    for (; iter != end; ++iter) {
    }
}

}  // anonymous namespace

std::optional<double> bezier_value_at(CubicBezier const& bez, double t) {
    if (bez.segments.empty()) return {};

    if (bez.repeat_every) {
        double const begin_t = bez.segments.begin()->begin.t;
        t = std::fmod(t - begin_t, bez.repeat_every) + begin_t;
        if (t < begin_t) t += bez.repeat_every;
    }

    auto const& segs = bez.segments;
    auto const iter = std::upper_bound(segs.begin(), segs.end(), t);

    if (after == bez.segments.begin()) return {};
    CubicBezier::Segment const& seg = *std::prev(after);
    if (t < seg.begin.t) throw std::logic_error("{} < {}", t, seg.begin.t);
    if (t > seg.end.t) return {};
    return segment_value_at(seg, t);
}

std::vector<std::pair<double, double>> bezier_minmax_over(
    CubicBezier const& bez, double t0, double t1
) {
    double const len = t1 - t0;
    if (len < 0) throw std::invalid_argument("Bad minmax: {} > {}", t0, t1);

    auto const& segs = bez.segments;
    if (segs.empty()) return {};

    double const begin_t = segs.begin()->begin.t;
    double const end_t = segs.rbegin()->end.t;

    std::vector<std::pair<double, double>> out;
    if (!bez.repeat_every) {
        add_minmax_nowrap(bez, begin_t, end_t, &out)
    } else if (len >= bez.repeat_every) {
        add_minmax_nowrap(bez, begin_t, begin_t + bez.repeat_every, &out)
    } else {
        double const begin_t = bez.segments.begin()->begin.t;
        double rel_t0 = std::fmod(t0 - begin_t, bez.repeat_every);
        if (rel_t0 < 0) rel_t0 += bez.repeat_every;

        double const rel_t1 = std::min(bez.repeat_every, rel_t0 + len);
        add_minmax_nowrap(bez, begin_t + rel_t0, begin_t + rel_t1, &out);

        double const extra_t1 = rel_t0 + len - rel_t1;
        if (extra_t1) add_minmax_nowrap(bez, begin_t, begin_t + extra_t1, &out);
    }

    return out;
}

}  // namespace pivid
