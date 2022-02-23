// Interfaces to show images (using display_output.h) at precise times.

#pragma once

#include <chrono>
#include <map>
#include <memory>

#include "display_output.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

// Interface to an asynchronous thread that shows images in timed sequence.
// *Internally synchronized* for multithreaded access.
class FramePlayer {
  public:
    // Sequence of frames with "monotonic clock" display time.
    // Each frame is a stack of layers to pass to DisplayDriver::update().
    using Timeline = std::map<SteadyTime, std::vector<DisplayLayer>>;

    // Interrupts and shuts down the frame player.
    virtual ~FramePlayer() = default;

    // Sets the list of frames to play. These are uncompressed; normally this
    // is limited to a short near-term buffer and periodically refreshed.
    // The signal (if any) is set when frames are shown.
    virtual void set_timeline(Timeline, std::shared_ptr<ThreadSignal> = {}) = 0;

    // Returns the *scheduled* time of the most recently played frame.
    // (TODO: Make the actual time it was displayed also available.)
    virtual SteadyTime last_shown() const = 0;
};

std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<UnixSystem>, DisplayDriver*, uint32_t id, DisplayMode
);

}  // namespace pivid
