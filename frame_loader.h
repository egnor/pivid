// Interfaces to preload and cache frames from media files.

#pragma once

#include <functional>
#include <map>
#include <memory>

#include "media_decoder.h"
#include "display_output.h"
#include "interval_set.h"
#include "thread_signal.h"
#include "unix_system.h"

namespace pivid {

class FrameLoader {
  public:
    struct Loaded {
        std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
        std::optional<Seconds> eof;
        IntervalSet<Seconds> done;
    };

    virtual ~FrameLoader() = default;

    virtual void set_request(
        IntervalSet<Seconds> const&,
        std::shared_ptr<ThreadSignal> = {}
    ) = 0;

    virtual Loaded loaded() const = 0;
};

std::unique_ptr<FrameLoader> make_frame_loader(
    DisplayDriver*,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> =
        open_media_decoder
);

std::string debug(Interval<Seconds> const&);
std::string debug(IntervalSet<Seconds> const&);

}  // namespace pivid
