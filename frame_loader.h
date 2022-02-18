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
        Millis begin = {}, end = {};
        double max_priority = 1.0, min_priority = 0.0;
        bool final = false;
    };

    using Frames = std::map<Millis, std::shared_ptr<LoadedImage>>;

    static Millis constexpr eof{999999999};

    virtual ~FrameWindow() = default;

    virtual void set_request(Request const&) = 0;

    virtual Frames loaded() const = 0;

    virtual Millis load_progress() const = 0;
};

class FrameLoader {
  public:
    virtual ~FrameLoader() = default;

    virtual std::unique_ptr<FrameWindow> open_window(
        std::unique_ptr<MediaDecoder>
    ) = 0;
};

std::unique_ptr<FrameLoader> make_frame_loader(DisplayDriver*);

std::string debug(FrameWindow::Request const&);

}  // namespace pivid
