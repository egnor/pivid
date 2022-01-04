#pragma once

#include <memory>
#include <string>
#include <vector>

#include "frame_buffer.h"

namespace pivid {

struct MediaFrame {
    double time;
    std::vector<std::shared_ptr<FrameBuffer const>> layers;
    std::string_view frame_type;
    bool is_key_frame;
    bool is_corrupt;
    bool at_eof;
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
    virtual bool next_frame_ready() = 0;
    virtual MediaFrame next_frame() = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& url);

}  // namespace pivid
