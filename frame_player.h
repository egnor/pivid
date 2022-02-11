// Interfaces to show images (using display_output.h) at precise times.

#pragma once

#include <chrono>
#include <map>

#include "display_output.h"

namespace pivid {

// Interface to an asynchronous thread that shows images in timed sequence.
// *Internally synchronized* for multithreaded access.
class FramePlayer {
  public:
    using Timeline = std::map<
        std::chrono::steady_clock::time_point,
        std::vector<DisplayImage>
    >;

    // Interrupts and shuts down the frame player.
    virtual ~FramePlayer() = default;

    // Sets the list of frames to play. These are uncompressed; normally this
    // is limited to a short near-term buffer and periodically refreshed.
    virtual void set_timeline(Timeline) = 0;

    // Returns the scheduled time of the most recently played frame.
    virtual Timeline::key_type last_shown() const = 0;
};

std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<UnixSystem>, DisplayDriver*, uint32_t id, DisplayMode
);

}  // namespace pivid
