#pragma once

#include <memory>
#include <string>

namespace pivid {

class MediaError : public std::exception {};

class MediaFrame {
  public:
    virtual ~MediaFrame() {}
};

class MediaDecoder {
  public:
    virtual ~MediaDecoder() {}
    virtual std::unique_ptr<MediaFrame> next_frame() = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& url);

}  // namespace pivid
