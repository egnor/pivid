#pragma once

#include <memory>
#include <string>
#include <vector>

#include "image_buffer.h"

namespace pivid {

struct MediaFrame {
    double time;
    std::vector<ImageBuffer> layers;
    std::string_view frame_type;
    bool is_key_frame;
    bool is_corrupt;
};

struct MediaInfo {
    std::string container_type;
    std::string codec_name;
    std::string pixel_format;
    std::optional<int> width, height;
    std::optional<double> duration;
    std::optional<double> frame_rate;
    std::optional<int64_t> bit_rate;
};

class MediaDecoder {
  public:
    virtual ~MediaDecoder() {}
    virtual MediaInfo const& info() const = 0;
    virtual bool reached_eof() = 0;
    virtual bool next_frame_ready() = 0;
    virtual MediaFrame get_next_frame() = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& url);

}  // namespace pivid
