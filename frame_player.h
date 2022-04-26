// Interface to show images (using display_output.h) at precise times.

#pragma once

#include <map>
#include <memory>

#include "display_output.h"
#include "unix_system.h"

namespace pivid {

// Interface to an asynchronous thread that shows images in timed sequence.
// *Internally synchronized* for multithreaded access.
class FramePlayer {
  public:
    // Sequence of frames with system clock display time.
    using Timeline = std::map<double, DisplayFrame>;

    // Interrupts and shuts down the frame player.
    virtual ~FramePlayer() = default;

    // Sets the list of frames to play. These are uncompressed; normally this
    // is limited to a short near-term buffer and periodically refreshed.
    // The signal (if any) is set when frames are shown.
    virtual void set_timeline(Timeline, std::shared_ptr<SyncFlag> = {}) = 0;

    // Returns the *scheduled* time of the most recently shown frame.
    // (TODO: Make DisplayOutputDone also available.)
    virtual double last_shown() const = 0;
};

// Creates a frame player instance for a given driver and screen.
std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<DisplayDriver>, uint32_t screen_id,
    std::shared_ptr<UnixSystem> = global_system()
);

}  // namespace pivid
