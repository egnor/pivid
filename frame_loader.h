// Interfaces to preload and cache frames from media files.

#pragma once

#include <exception>
#include <functional>
#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "interval.h"
#include "unix_system.h"

namespace pivid {

// Interface to an asynchronous thread that loads frames from media into GPU.
// *Internally synchronized* for multithreaded access.
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
        std::shared_ptr<SyncFlag> = {}
    ) = 0;

    // Returns the frames loaded so far.
    virtual Content content() const = 0;

    // Returns static metadata for the media file.
    virtual MediaFileInfo file_info() const = 0;
};

struct FrameLoaderContext {
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<DisplayDriver> driver;
    std::string filename;
    double decoder_idle_time = 1.0;
    double seek_scan_time = 1.0;

    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> decoder_f;
};

// Creates a frame loader instance for a given media file and GPU device.
std::unique_ptr<FrameLoader> start_frame_loader(FrameLoaderContext);

}  // namespace pivid
