// Interfaces to preload and cache frames from media files.

#pragma once

#include <functional>
#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

class FrameLoader {
  public:
    struct Frames {
        std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
        std::map<Seconds, Seconds> cover;
        bool includes_eof = false;
    };

    struct Request {
        Seconds begin = {}, end = {};
    };

    virtual ~FrameLoader() = default;

    virtual void set_wanted(
        std::vector<Request> const&,
        std::shared_ptr<ThreadSignal> = {}
    ) = 0;

    virtual Frames loaded() const = 0;
};

std::unique_ptr<FrameLoader> make_frame_loader(
    DisplayDriver*,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

std::string debug(FrameLoader::Request const&);

}  // namespace pivid
