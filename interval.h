#pragma once

#include <compare>
#include <set>
#include <string>

namespace pivid {

struct Interval {
    double begin = {}, end = {};

    bool empty() const { return begin >= end; }
    bool contains(double t) const { return begin <= t && t < end; }
    auto operator<=>(Interval const& o) const { return begin <=> o.begin; }
    bool operator==(Interval const& o) const = default;
};

class IntervalSet {
  public:
    using iterator = std::set<Interval>::const_iterator;

    iterator insert(Interval);
    void insert(IntervalSet const& s) { for (auto r : s) insert(r); }

    iterator erase(Interval);
    void erase(IntervalSet const& s) { for (auto r : s) erase(r); }

    iterator begin() const { return ranges.begin(); }
    iterator end() const { return ranges.end(); }
    bool empty() const { return ranges.empty(); }
    int count() const { return ranges.size(); }

    iterator overlap_begin(double t) const;
    iterator overlap_end(double t) const { return ranges.lower_bound({t, {}}); }
    bool contains(double) const;
    Interval bounds() const;

    auto operator<=>(IntervalSet const& o) const = default;
    bool operator==(IntervalSet const& o) const = default;

  private:
    std::set<Interval> ranges;
};

// Debugging descriptions of values.
std::string debug(Interval);
std::string debug(IntervalSet const&);

}  // namespace pivid
