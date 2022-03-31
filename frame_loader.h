// Interfaces to preload and cache frames from media files.

#pragma once

#include <exception>
#include <functional>
#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "interval.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

// Interface to an asynchronous thread that loads frames from media into GPU.
class FrameLoader {
  public:
    // Currently loaded frames.
    struct Content {
        std::map<double, std::shared_ptr<LoadedImage>> frames;
        IntervalSet have;  // Regions that are fully loaded
        std::optional<double> eof;  // Where EOF is, if known
        std::exception_ptr error;  // Last major error, if any
    };

    // Interrupts and shuts down the frame loader.
    virtual ~FrameLoader() = default;

    // Sets the regions of interest to load, discarding frames outside them.
    virtual void set_request(
        IntervalSet const&,
        std::shared_ptr<ThreadSignal> = {}
    ) = 0;

    // Returns the frames loaded so far.
    virtual Content content() const = 0;
};

// Creates a frame loader instance for a given media file and GPU device.
std::unique_ptr<FrameLoader> start_frame_loader(
    std::shared_ptr<DisplayDriver>,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

}  // namespace pivid
