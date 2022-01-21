#pragma once

#include <compare>
#include <memory>
#include <string>
#include <vector>

namespace pivid {

// Description of a memory buffer holding image data.
// Zero-copy DMA buffers may not be memory mapped by default, but
// read() always maps the buffer into userspace for access if needed.
class MemoryBuffer {
  public:
    virtual ~MemoryBuffer() = default;
    virtual size_t size() const = 0;                   // Size in bytes
    virtual uint8_t const* read() = 0;                 // Memory-mapped data
    virtual int dma_fd() const { return -1; }          // "DMA-buf" descriptor
    virtual uint32_t drm_handle() const { return 0; }  // DRM buffer handle
};

// Implementation of MemoryBuffer in an ordinary userspace vector.
class PlainMemoryBuffer : public MemoryBuffer {
  public:
    PlainMemoryBuffer(size_t size) { mem.resize(size); }
    virtual size_t size() const { return mem.size(); }
    virtual uint8_t const* read() { return mem.data(); }
    uint8_t* write() { return mem.data(); }
  private:
    std::vector<uint8_t> mem;
};

// Description of a pixel image stored in one or more MemoryBuffer objects.
// Returned from MediaDecoder::next_frame() (in MediaFrame) or built "by hand",
// supplied to DisplayDriver::load_image().
struct ImageBuffer {
    // Some image formats (like YUV420) use multiple channels (aka "planes").
    struct Channel {
        std::shared_ptr<MemoryBuffer> memory;  // Channel data is stored here
        ptrdiff_t offset = 0;                  // Offset within memory
        ptrdiff_t stride = 0;                  // Bytes between lines
    };

    // Image format uses FourCC (fourcc.org) and DRM "modifiers" as in
    // https://www.kernel.org/doc/html/latest/gpu/drm-kms.html#format-modifiers
    uint32_t fourcc = 0;    // Generally from the ffmpeg format list
    uint64_t modifier = 0;  // See the DRM document above
    int width = 0;          // The pixel size of the image
    int height = 0;
    std::vector<Channel> channels;  // Channel count depends on the format
};

// Assembles a fourcc uint32_t from text (like "RGBA").
constexpr uint32_t fourcc(char const c[4]) {
    return c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
}

// Debugging descriptions of values and structures.
std::string debug_fourcc(uint32_t);
std::string debug_size(size_t);
std::string debug(MemoryBuffer const&);
std::string debug(ImageBuffer const&);

}  // namespace pivid
