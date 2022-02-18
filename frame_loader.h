// Interfaces to preload and cache frames from media files.

#pragma once

#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"

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
        Seconds begin = {}, end = {};  // Time range within the media file
        double max_priority = 1.0, min_priority = 0.0;
        bool freeze = false;  // If true, discard the decoder after loading
    };

    // Sequence of GPU-loaded frames indexed by time within the media file.
    using Frames = std::map<Seconds, std::shared_ptr<LoadedImage>>;

    // Sentinel value returned by load_progress() when EOF was encountered.
    static constexpr Seconds eof{999999999};

    // Discards these cached frames.
    virtual ~FrameWindow() = default;

    // Updates the window parameters, retaining frames if they overlap.
    // Loading starts in the background after the request is set or changed.
    // If 'freeze' is true, set_request() may not be called again.
    virtual void set_request(Request const&) = 0;

    // Returns frames loaded in the window so far. Frames are always loaded
    // from the beginning of the range forward, up to one frame past the end.
    // This value changes as frames are loaded asynchronously.
    virtual Frames loaded() const = 0;

    // Returns the latest frame loaded, or FrameWindow::eof if EOF was found.
    // The window is fully populated when load_progress() >= request.end.
    // This value changes as frames are loaded asynchronously.
    virtual Seconds load_progress() const = 0;
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
std::unique_ptr<FrameLoader> make_frame_loader(DisplayDriver*);

// Debugging description of the request structure.
std::string debug(FrameWindow::Request const&);

}  // namespace pivid
