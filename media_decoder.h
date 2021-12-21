#pragma once

#include <memory>
#include <string>

struct AVFrame;
struct AVDRMFrameDescriptor;

namespace pivid {

class MediaError : public std::exception {};

class MediaFrame {
  public:
    virtual ~MediaFrame() {}
    virtual AVFrame const& frame() = 0;
    virtual AVDRMFrameDescriptor const& drm() = 0;
};

class MediaDecoder {
  public:
    virtual ~MediaDecoder() {}
    virtual std::unique_ptr<MediaFrame> next_frame() = 0;
    virtual bool at_eof() const = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(std::string const& url);

}  // namespace pivid
