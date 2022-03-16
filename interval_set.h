#pragma once

#include <compare>
#include <set>

namespace pivid {

template <typename T>
struct Interval {
    T begin = {}, end = {};

    bool empty() const { return begin >= end; }
    bool contains(T t) const { return begin <= t && t < end; }
    auto operator<=>(Interval const& o) const { return begin <=> o.begin; }
    bool operator==(Interval const& o) const = default;
};

template <typename T>
class IntervalSet {
  public:
    using Interval = pivid::Interval<T>;
    using iterator = std::set<Interval>::const_iterator;

    iterator insert(Interval);
    void insert(IntervalSet const& s) { for (auto r : s) insert(r); }

    iterator erase(Interval);
    void erase(IntervalSet const& s) { for (auto r : s) erase(r); }

    iterator begin() const { return ranges.begin(); }
    iterator end() const { return ranges.end(); }
    bool empty() const { return ranges.empty(); }
    int count() const { return ranges.size(); }

    iterator overlap_begin(T t) const;
    iterator overlap_end(T t) const { return ranges.lower_bound({t, {}}); }
    bool contains(T) const;

    auto operator<=>(IntervalSet const& o) const = default;
    bool operator==(IntervalSet const& o) const = default;

  private:
    std::set<Interval> ranges;
};

//
// Implementation
//

template <typename T>
IntervalSet<T>::iterator IntervalSet<T>::insert(Interval add) {
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

template <typename T>
IntervalSet<T>::iterator IntervalSet<T>::erase(Interval remove) {
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

template <typename T>
IntervalSet<T>::iterator IntervalSet<T>::overlap_begin(T value) const {
    auto const next_after = ranges.upper_bound({value, {}});
    if (next_after == ranges.begin()) return next_after;
    auto const at_or_before = std::prev(next_after);
    return (at_or_before->end > value) ? at_or_before : next_after;
}

template <typename T>
bool IntervalSet<T>::contains(T value) const {
    auto const iter = overlap_begin(value);
    return iter != ranges.end() && iter->begin <= value;
}

}  // namespace pivid
