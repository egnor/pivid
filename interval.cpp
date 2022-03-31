#include "interval.h"

#include "fmt/core.h"

namespace pivid {

IntervalSet::iterator IntervalSet::insert(Interval add) {
    if (add.begin >= add.end) return ranges.end();

    auto next_contact = ranges.upper_bound(add);
    if (next_contact != ranges.begin()) {
        auto const at_or_before = std::prev(next_contact);
        if (at_or_before->end >= add.end) return next_contact;
        if (at_or_before->end >= add.begin) {
            next_contact = at_or_before;
            add.begin = at_or_before->begin;
        }
    }

    while (next_contact != ranges.end() && next_contact->begin <= add.end) {
        if (next_contact->end > add.end) add.end = next_contact->end;
        next_contact = ranges.erase(next_contact);
    }

    return ranges.insert(add).first;
}

IntervalSet::iterator IntervalSet::erase(Interval remove) {
    if (remove.begin >= remove.end) return ranges.end();

    auto next_overlap = ranges.upper_bound(remove);
    if (next_overlap != ranges.begin()) {
        auto const at_or_before = std::prev(next_overlap);
        if (at_or_before->end > remove.begin) next_overlap = at_or_before;
    }

    while (next_overlap != ranges.end() && next_overlap->begin < remove.end) {
        auto const overlap = *next_overlap;
        next_overlap = ranges.erase(next_overlap);
        if (overlap.begin < remove.begin)
            ranges.insert({overlap.begin, remove.begin});
        if (overlap.end > remove.end)
            ranges.insert({remove.end, overlap.end});
    }

    return next_overlap;
}

IntervalSet::iterator IntervalSet::overlap_begin(double value) const {
    auto const next_after = ranges.upper_bound({value, {}});
    if (next_after == ranges.begin()) return next_after;
    auto const at_or_before = std::prev(next_after);
    return (at_or_before->end > value) ? at_or_before : next_after;
}

bool IntervalSet::contains(double value) const {
    auto const iter = overlap_begin(value);
    return iter != ranges.end() && iter->begin <= value;
}

Interval IntervalSet::bounds() const {
    if (empty()) return {};
    return {ranges.begin()->begin, ranges.rbegin()->end};
}

std::string debug(Interval interval) {
    return fmt::format("{:.3f}~{:.3f}s", interval.begin, interval.end);
}

std::string debug(IntervalSet const& interval_set) {
    std::string out = "{";
    for (auto const& interval : interval_set) {
        if (out.size() > 1) out += ", ";
        out += pivid::debug(interval);
    }
    return out + "}";
}

}  // namespace pivid
