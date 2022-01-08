#pragma once

#include <compare>
#include <memory>
#include <string>
#include <vector>

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

constexpr uint32_t fourcc(char const c[4]) {
    return c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
}

std::shared_ptr<MemoryBuffer> plain_memory_buffer(size_t);

std::string debug_size(size_t);
std::string debug_string(MemoryBuffer const&);
std::string debug_string(ImageBuffer const&);

}  // namespace pivid
