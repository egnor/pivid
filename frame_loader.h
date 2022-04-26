// Interface to preload and cache frames from media files.

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

// Request made to a FrameLoader.
struct FrameRequest {
    IntervalSet wanted;                // Which frames to load
    std::shared_ptr<SyncFlag> notify;  // If non-nullptr, notify on frame load
    double decoder_idle_time = 1.0;    // Tuning: delete decoders idle this long
    double seek_scan_time = 1.0;       // Tuning: scan instead of short seeks
};

// Current state from a FrameLoader.
struct LoadedFrames {
    std::map<double, std::shared_ptr<LoadedImage>> frames;  // Loaded frames
    IntervalSet coverage;       // Regions that are now fully loaded
    std::optional<double> eof;  // Where EOF is, if known
    std::exception_ptr error;   // Last major error, if any
};

// Interface to an asynchronous thread that loads frames from media into GPU.
// *Internally synchronized* for multithreaded access.
class FrameLoader {
  public:
    // Interrupts and shuts down the frame loader.
    virtual ~FrameLoader() = default;

    // Sets the regions of interest to load, discarding frames outside them.
    virtual void set_request(FrameRequest) = 0;

    // Returns the frames loaded so far.
    virtual LoadedFrames frames() const = 0;

    // Returns static metadata for the media file.
    virtual MediaFileInfo file_info() const = 0;
};

// Resources and parameters needed to start a FrameLoader.
struct FrameLoaderContext {
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<DisplayDriver> driver;
    std::string filename;  // The media file the loader will be reading
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> decoder_f;
};

// Creates a frame loader instance for a given GPU device and media file.
std::unique_ptr<FrameLoader> start_frame_loader(FrameLoaderContext);

}  // namespace pivid
