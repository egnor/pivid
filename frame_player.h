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

struct TimedFrameDone {
    std::chrono::steady_clock::time_point frame_time;
    DisplayUpdateDone display;
};

// Interface to an asynchronous thread that shows images in timed sequence.
class FramePlayer {
  public:
    virtual ~FramePlayer() = default;

    // Set the list of frames to play. These are uncompressed; normally this
    // is limited to a short near-term buffer and periodically refreshed.
    virtual void set_frames(std::vector<TimedFrame> frames) = 0;

    // Returns the vector index [0,frames.size()] of the first unshown frame.
    virtual int next_index() const = 0;

    // Returns timestamp and status of the last frame shown on the screen.
    virtual std::optional<TimedFrameDone> last_shown() const = 0;
};

}  // namespace pivid
