#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "image_buffer.h"

namespace pivid {

struct MediaInfo {
    std::string filename;
    std::string container_type;
    std::string codec_name;
    std::string pixel_format;
    std::optional<int> width, height;
    std::optional<double> duration;
    std::optional<double> frame_rate;
    std::optional<int64_t> bit_rate;
};

struct MediaFrame {
    double time = 0;
    std::vector<ImageBuffer> images;
    std::string_view frame_type;
    bool is_key_frame = false;
    bool is_corrupt = false;
};

class MediaDecoder {
  public:
    virtual ~MediaDecoder() {}
    virtual MediaInfo const& info() const = 0;
    virtual bool reached_eof() = 0;
    virtual std::optional<MediaFrame> get_frame_if_ready() = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& filename);

std::vector<uint8_t> debug_tiff(ImageBuffer const&);
std::string debug(MediaFrame const&);
std::string debug(MediaInfo const&);

}  // namespace pivid
