#pragma once

#include <map>
#include <stdexcept>

namespace pivid {

template <typename T>
class RangeSet {
  public:
    using RangePair = std::map<T, T>::value_type;
    using iterator = std::map<T, T>::const_iterator;

    void add(RangePair range);
    void remove(RangePair range);

    iterator begin() const { return ranges.begin(); }
    iterator end() const { return ranges.end(); }
    iterator next_after(T t) const { return ranges.upper_bound(t); }

    int operator<=>(RangeSet const&) const = default;

  private:
    std::map<T, T> ranges;
};

//
// Implementation
//

template <typename T>
void RangeSet<T>::add(RangePair range) {
    if (range.first > range.second)
        throw std::invalid_argument("Bad range order");

    auto iter = ranges.upper_bound(range.first);
    if (iter == ranges.begin() || (--iter)->second < range.first) {
        iter = ranges.insert(range).first;
    } else if (iter->second >= range.second) {
        return;  // Range is already covered.
    } else {
        iter->second = range.second;
    }

    auto next = std::next(iter);
    while (next != ranges.end() && next->first <= iter->second) {
        if (next->second > iter->second) {
            iter->second = next->second;
            ranges.erase(next);
            break;
        }
        next = ranges.erase(next);
    }
}

template <typename T>
void RangeSet<T>::remove(RangePair range) {
    if (range.first > range.second)
        throw std::invalid_argument("Bad range order");

    auto iter = ranges.lower_bound(range.first);
    if (iter != ranges.begin()) {
        const auto prev = std::prev(iter);
        if (prev->second > range.first)
            prev->second = range.first;
    }

    while (iter != ranges.end() && iter->first < range.second) {
        if (iter->second > range.second) {
            ranges.insert({range.second, iter->second});
            ranges.erase(iter);
            break;
        }
        iter = ranges.erase(iter);
    }
}

}  // namespace pivid
