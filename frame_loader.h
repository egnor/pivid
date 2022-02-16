// Interfaces to preload and cache frames from various media files.

#pragma once

#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"

namespace pivid {

class FrameWindow {
  public:
    struct Request {
        std::chrono::milliseconds start = {}, end = {};
        double start_priority = 1.0, end_priority = 0.0;
        bool keep_decoder = true;
    };

    using Frames = std::map<
        std::chrono::milliseconds,
        std::shared_ptr<LoadedImage>
    >;

    virtual ~FrameWindow() = default;

    virtual void set_request(Request const&) = 0;

    virtual Frames loaded() const = 0;
};

class FrameLoader {
  public:
    virtual ~FrameLoader() = default;

    virtual std::unique_ptr<FrameWindow> open_window(
        std::unique_ptr<MediaDecoder>
    ) = 0;
};

std::unique_ptr<FrameLoader> make_frame_loader(DisplayDriver*);

}  // namespace pivid
