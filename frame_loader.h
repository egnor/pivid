// Interfaces to preload and cache frames from media files.

#pragma once

#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "unix_system.h"

namespace pivid {

// Access to frames loaded into a GPU from a particular time range within a
// media file (typically a section being played, or preparing to be played).
// Frames load asynchronously, and refill if the range is expanded or shifted.
//
// Returned by FrameLoader::open_window() using a MediaDecoder instance.
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

    // Frame cache contents.
    struct Results {
        std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
        bool filled = false;
        bool at_eof = false;
    };

    // Discards these cached frames.
    virtual ~FrameWindow() = default;

    // Updates the window parameters and starts loading in the background.
    // If .freeze is true, set_request() may not be called again.
    // If .signal is present, it will be set when frames load.
    virtual void set_request(Request const&) = 0;

    // Returns the frames loaded into the window so far.
    // (The window is always filled from request.begin forward.)
    virtual Results results() const = 0;
};

// Interface to manage GPU frame caching across files and time windows.
class FrameLoader {
  public:
    virtual ~FrameLoader() = default;

    // Returns a new FrameWindow to cache frames from a given media decoder.
    // Cached frames are shared across decoders for the same file.
    virtual std::unique_ptr<FrameWindow> open_window(
        std::unique_ptr<MediaDecoder>
    ) = 0;
};

// Creates a FrameLoader targeted to a particular GPU.
std::unique_ptr<FrameLoader> make_frame_loader(
    std::shared_ptr<UnixSystem> sys, DisplayDriver*
);

// Debugging description of the request structure.
std::string debug(FrameWindow::Request const&);

}  // namespace pivid
