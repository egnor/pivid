#pragma once

#include "unix_system.h"

namespace pivid {

// A simple synchronization event (like a simpler version of C++20
// std::counting_semaphore, which isn't implemented in our libstdc++).
// *Internally synchronized* for use from multiple threads.
// TODO: Replace with std::counting_semaphore once available in distros
class ThreadSignal {
  public:
    virtual ~ThreadSignal() = default;

    // Increments the internal signal count by 1.
    virtual void set() = 0;

    // Waits until the signal count is nonzero, then decrement it by one.
    virtual void wait() = 0;

    // Waits until the signal count is nonzero (as above) OR the time given.
    // Returns true if a signal was found.
    virtual bool wait_until(SteadyTime) = 0;
};

std::shared_ptr<ThreadSignal> make_signal();

}  // namespace pivid
