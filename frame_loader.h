// Interfaces to preload and cache frames from media files.

#pragma once

#include <functional>
#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "range_set.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

class FrameLoader {
  public:
    struct Results {
        std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
        RangeSet<Seconds> done;
    };

    virtual ~FrameLoader() = default;

    virtual void set_request(
        RangeSet<Seconds> const&,
        std::shared_ptr<ThreadSignal> = {}
    ) = 0;

    virtual Results results() const = 0;
};

std::unique_ptr<FrameLoader> make_frame_loader(
    DisplayDriver*,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

std::string debug(Range<Seconds> const&);
std::string debug(RangeSet<Seconds> const&);

}  // namespace pivid
