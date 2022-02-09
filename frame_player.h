// Interfaces to show images (using display_output.h) at precise times.

#pragma once

#include <chrono>

#include "display_output.h"

namespace pivid {

// Screen contents to be shown after a specific clock time.
struct TimedFrame {
    std::chrono::steady_clock::time_point time;
    std::vector<DisplayImage> images;
};

// 
class FramePlayer {
  public:
    virtual ~FramePlayer() = default;

    virtual void set_plan(std::vector<TimedFrame> frames) = 0;
};

}  // namespace pivid
