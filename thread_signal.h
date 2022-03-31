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

    // Sets the internal signal flag.
    virtual void set() = 0;

    // Returns the internal signal flag without modification.
    virtual bool test() const = 0;

    // Waits until the signal flag is set, then resets it and returns.
    virtual void wait() = 0;

    // Waits until the signal flag is set (as above) OR the time given.
    // Returns true if a signal was found (and cleared).
    virtual bool wait_until(double t) = 0;
};

std::unique_ptr<ThreadSignal> make_signal();

}  // namespace pivid
