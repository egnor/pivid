#pragma once

#include <compare>
#include <memory>

namespace pivid {

class MemoryBuffer {
  public:
    virtual ~MemoryBuffer() {}
    int dma_fd() const = 0;
};

struct FrameBuffer {
    struct Channel {
        std::shared_ptr<MemoryBuffer> memory;
        int start_offset;
        int bytes_per_line;
        auto operator<=>(Channel const&) const = default;
    };

    std::vector<Channel> channels;
    uint32_t fourcc;
    uint64_t modifier;
    int width;
    int height;
    auto operator<=>(FrameBuffer const&) const = default;
};

}  // namespace pivid
