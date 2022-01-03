#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frame_buffer.h"

namespace pivid {

struct DecodedFrame {
    std::vector<FrameBuffer> layers;
    double time;
    bool is_corrupt;
    bool is_key_frame;
};

struct MediaInfo {
    std::string container_type;
    std::string codec_name;
    std::string pixel_format;
    double start_time;
    double duration;
    double bit_rate;
    double frame_rate;
    int frame_count;
    int width, height;
};

class MediaDecoder {
  public:
    virtual ~MediaDecoder() {}
    virtual MediaInfo const& info() const = 0;
    virtual std::optional<DecodedFrame> next_frame() = 0;
    virtual bool at_eof() const = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& url);

}  // namespace pivid
