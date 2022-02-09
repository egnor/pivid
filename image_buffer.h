// Data structures for in-memory buffered images.

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

// Description of a pixel image stored in one or more MemoryBuffer objects.
// Returned from MediaDecoder::next_frame() (in MediaFrame) or built "by hand";
// passed to DisplayDriver::load_image().
//
// Image format is described with FourCC (fourcc.org) as used by ffmpeg:
// github.com/jc-kynesim/rpi-ffmpeg/blob/release/4.3/rpi_main/libavcodec/raw.c
// and format "modifiers" as defined by the Linux kernel:
// kernel.org/doc/html/latest/gpu/drm-kms.html#format-modifiers
struct ImageBuffer {
    // Some image formats (like YUV420) use multiple channels (aka "planes").
    // Channels may use different buffers or offsets within the same buffer.
    struct Channel {
        std::shared_ptr<MemoryBuffer> memory;  // Channel data is stored here
        ptrdiff_t offset = 0;                  // Start offset within buffer
        ptrdiff_t stride = 0;                  // Offset between scanlines
    };

    uint32_t fourcc = 0;    // Image pixel layout, like fourcc("RGBA")
    uint64_t modifier = 0;  // Modifier to image pixel layout
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
