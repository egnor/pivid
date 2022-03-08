#pragma once

#include <compare>
#include <set>

namespace pivid {

template <typename T>
struct Range {
    T begin, end;
    auto operator<=>(Range const& o) const { return begin <=> o.begin; }
    bool operator==(Range const& o) const = default;
};

template <typename T>
class RangeSet {
  public:
    using Range = pivid::Range<T>;
    using iterator = std::set<Range>::const_iterator;

    void insert(Range);
    void insert(RangeSet const& s) { for (auto r : s) insert(r); }

    void erase(Range);
    void erase(RangeSet const& s) { for (auto r : s) erase(r); }

    iterator begin() const { return ranges.begin(); }
    iterator end() const { return ranges.end(); }
    int size() const { return ranges.size(); }

    iterator overlap_begin(T t) const;
    iterator overlap_end(T t) const { return ranges.lower_bound({t, {}}); }
    bool contains(T) const;

    auto operator<=>(RangeSet const& o) const = default;
    bool operator==(RangeSet const& o) const = default;

  private:
    std::set<Range> ranges;
};

//
// Implementation
//

template <typename T>
void RangeSet<T>::insert(Range add) {
    if (add.begin >= add.end) return;

    auto next_contact = ranges.upper_bound(add);
    if (next_contact != ranges.begin()) {
        auto const at_or_before = std::prev(next_contact);
        if (at_or_before->end >= add.end) return;
        if (at_or_before->end >= add.begin) {
            next_contact = at_or_before;
            add.begin = at_or_before->begin;
        }
    }

    while (next_contact != ranges.end() && next_contact->begin <= add.end) {
        if (next_contact->end > add.end) add.end = next_contact->end;
        next_contact = ranges.erase(next_contact);
    }

    ranges.insert(add);
}

template <typename T>
void RangeSet<T>::erase(Range remove) {
    if (remove.begin >= remove.end) return;

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
}

template <typename T>
RangeSet<T>::iterator RangeSet<T>::overlap_begin(T value) const {
    auto const next_after = ranges.upper_bound({value, {}});
    if (next_after == ranges.begin()) return next_after;
    auto const at_or_before = std::prev(next_after);
    return (at_or_before->end > value) ? at_or_before : next_after;
}

template <typename T>
bool RangeSet<T>::contains(T value) const {
    auto const iter = overlap_begin(value);
    return iter != ranges.end() && iter->begin <= value;
}

}  // namespace pivid
