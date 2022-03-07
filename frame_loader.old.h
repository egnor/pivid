// Interfaces to preload and cache frames from media files.

#pragma once

#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

// Access to asynchronously loaded frames within a particular time range
// (typically a section being/about to be played) from a MediaDecoder instance.
// *Internally synchronized* for multithreaded access.
class FrameWindow {
  public:
    // Description of the time range of interest within the media file.
    struct Request {
        Seconds begin = {}, end = {};
        double max_priority = 1.0, min_priority = 0.0;
        bool freeze = false;
        std::shared_ptr<ThreadSignal> signal = {};
    };

    // Loaded frames.
    struct Results {
        std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
        bool filled = false;
        bool at_eof = false;
    };

    // Discards the frame cache.
    virtual ~FrameWindow() = default;

    // Updates the time range and (re)starts background loading.
    // If .freeze is true, set_request() may not be called again.
    // If .signal is present, it will be set every time frames load.
    virtual void set_request(Request const&) = 0;

    // Returns the frames loaded so far (filled from request.begin forward).
    virtual Results results() const = 0;
};

// Interface to manage GPU frame caching across files and time windows.
class OldFrameLoader {
  public:
    virtual ~OldFrameLoader() = default;

    // Returns a new FrameWindow to cache frames from a given media decoder.
    // Cached frames are shared across decoders for the same file.
    virtual std::unique_ptr<FrameWindow> open_window(
        std::unique_ptr<MediaDecoder>
    ) = 0;
};

// Creates a FrameLoader targeted to a particular GPU.
std::unique_ptr<OldFrameLoader> make_frame_loader(DisplayDriver*);

// Debugging description of the request structure.
std::string debug(FrameWindow::Request const&);

}  // namespace pivid
