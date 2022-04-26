// Data structures for representing ranges of numbers (doubles).

#pragma once

#include <compare>
#include <set>
#include <string>

namespace pivid {

// A half-open interval [begin, end) on the number line.
struct Interval {
    double begin = {}, end = {};

    bool empty() const { return begin >= end; }
    bool contains(double t) const { return begin <= t && t < end; }
    auto operator<=>(Interval const& o) const { return begin <=> o.begin; }
    bool operator==(Interval const& o) const = default;
};

// A set of non-overlapping Intervals across the number line.
class IntervalSet {
  public:
    using iterator = std::set<Interval>::const_iterator;

    // Adds an Interval, merging as necessary for overlaps and adjacencies.
    iterator insert(Interval);

    // Adds every Interval in another IntervalSet.
    void insert(IntervalSet const& s) { for (auto r : s) insert(r); }

    // Removes an Interval, truncating or splitting intervals as necessary.
    iterator erase(Interval);

    // Removes every Interval in another IntervalSet.
    void erase(IntervalSet const& s) { for (auto r : s) erase(r); }

    // STL standard accessors for the Intervals in the set,
    // which are always non-overlapping, non-abutting and in sorted order.
    iterator begin() const { return ranges.begin(); }
    iterator end() const { return ranges.end(); }
    bool empty() const { return ranges.empty(); }
    int count() const { return ranges.size(); }

    // Returns the range of Intervals that overlap a given interval.
    iterator overlap_begin(double t) const;
    iterator overlap_end(double t) const { return ranges.lower_bound({t, {}}); }

    // Returns true if an interval in this set contains the given point.
    bool contains(double) const;

    // Returns the narrowest Interval that covers every Interval in the set.
    Interval bounds() const;

    auto operator<=>(IntervalSet const& o) const = default;
    bool operator==(IntervalSet const& o) const = default;

  private:
    std::set<Interval> ranges;
};

// Debugging descriptions of structures.
std::string debug(Interval);
std::string debug(IntervalSet const&);

}  // namespace pivid
