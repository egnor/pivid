#pragma once

#include <compare>
#include <memory>

namespace pivid {

class MemoryBuffer {
  public:
    virtual ~MemoryBuffer() {}
    virtual size_t size() const = 0;
    virtual uint8_t const* read() = 0;
    virtual uint8_t* write() = 0;
    virtual int write_count() const = 0;
    virtual std::optional<int> dma_fd() const { return {}; }
    virtual std::optional<uint32_t> drm_handle() const { return {}; }
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
