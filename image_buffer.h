#pragma once

#include <compare>
#include <memory>

namespace pivid {

class MemoryBuffer {
  public:
    virtual ~MemoryBuffer() {}
    virtual size_t buffer_size() const = 0;
    virtual int dma_fd() const { return -1; }
    virtual uint32_t drm_handle() const { return 0; }
    virtual uint8_t* mapped() { return nullptr; }
};

struct ImageBuffer {
    struct Channel {
        std::shared_ptr<MemoryBuffer> memory;
        ptrdiff_t memory_offset;
        ptrdiff_t line_stride;
        auto operator<=>(Channel const&) const = default;
    };

    std::vector<Channel> channels;
    uint32_t fourcc;
    uint64_t modifier;
    int width;
    int height;
    auto operator<=>(ImageBuffer const&) const = default;
};

}  // namespace pivid
