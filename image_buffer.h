#pragma once

#include <compare>
#include <memory>
#include <string>
#include <vector>

namespace pivid {

class MemoryBuffer {
  public:
    virtual ~MemoryBuffer() = default;
    virtual size_t size() const = 0;
    virtual uint8_t const* read() = 0;
    virtual int dma_fd() const { return -1; }
    virtual uint32_t drm_handle() const { return 0; }
};

class PlainMemoryBuffer : public MemoryBuffer {
  public:
    PlainMemoryBuffer(size_t size) { mem.resize(size); }
    virtual size_t size() const { return mem.size(); }
    virtual uint8_t const* read() { return mem.data(); }
    uint8_t* write() { return mem.data(); }
  private:
    std::vector<uint8_t> mem;
};

struct ImageBuffer {
    struct Channel {
        std::shared_ptr<MemoryBuffer> memory;
        ptrdiff_t offset = 0;
        ptrdiff_t stride = 0;
    };

    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    int width = 0;
    int height = 0;
    std::vector<Channel> channels;
};

constexpr uint32_t fourcc(char const c[4]) {
    return c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
}

std::string debug_fourcc(uint32_t);
std::string debug_size(size_t);
std::string debug(MemoryBuffer const&);
std::string debug(ImageBuffer const&);

}  // namespace pivid
