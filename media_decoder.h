#pragma once

#include <memory>
#include <string>

namespace pivid {

class MediaFrame {
 public:
  virtual ~MediaFrame() {}
};

class MediaDecoder {
 public:
  virtual ~MediaDecoder() {}
  std::unique_ptr<MediaFrame> next_frame() = 0;
};

std::unique_ptr<MediaDecoder> new_media_decoder(const std::string& url);

}  // namespace pivid
