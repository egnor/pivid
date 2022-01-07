#include "image_buffer.h"

#include <drm_fourcc.h>

#include "fmt/core.h"

namespace pivid {

class PlainMemoryBuffer : public MemoryBuffer {
  public:
    virtual size_t size() const { return mem.size(); }
    virtual uint8_t const* read() { return mem.data(); }
    virtual uint8_t* write() { ++writes; return mem.data(); }
    virtual int write_count() const { return writes; }
    void init(size_t size) { mem.resize(size); }

  private:
    std::vector<uint8_t> mem;
    int writes = 0;
};

std::shared_ptr<MemoryBuffer> plain_memory_buffer(size_t size) {
    auto out = std::make_shared<PlainMemoryBuffer>();
    out->init(size);
    return out;
}

std::string debug_size(size_t s) {
    if (s < 1000) return fmt::format("{}B", s);
    if (s < 10240) return fmt::format("{:.1f}K", s / 1024.0);
    if (s < 1024000) return fmt::format("{}K", s / 1024);
    if (s < 10485760) return fmt::format("{:.1f}M", s / 1048576.0);
    if (s < 1048576000) return fmt::format("{}M", s / 1048576);
    return fmt::format("{:.1f}G", s / 1073741824.0);
}

std::string debug_string(MemoryBuffer const& mem) {
    std::string out = debug_size(mem.size());
    if (mem.dma_fd()) out += fmt::format(" fd{:<2d}", *mem.dma_fd());
    if (mem.drm_handle()) out += fmt::format(" h{:<2d}", *mem.drm_handle());
    return out;
}

std::string debug_string(ImageBuffer const& i) {
    std::string out = fmt::format(
        "{}x{} {:.4s}", i.width, i.height, (char const*) &i.fourcc
    );

    if (i.modifier) {
        auto const vendor = i.modifier >> 56;
        switch (vendor) {
#define V(x, y) case DRM_FORMAT_MOD_VENDOR_##x: out += y; break
            V(NONE, "");
            V(INTEL, ":INTL");
            V(AMD, ":AMD");
            V(NVIDIA, ":NVID");
            V(SAMSUNG, ":SAMS");
            V(QCOM, ":QCOM");
            V(VIVANTE, ":VIVA");
            V(BROADCOM, ":BCOM");
            V(ARM, ":ARM");
            V(ALLWINNER, ":ALLW");
            V(AMLOGIC, ":AML");
#undef V
            default: out += fmt::format(":{}", vendor);
        }
        out += fmt::format(":{:x}", i.modifier & ((1ull << 56) - 1));
    }

    for (size_t c = 0; c < i.channels.size(); ++c) {
        auto const& chan = i.channels[c];
        if (c > 0 && chan.memory == i.channels[c - 1].memory) {
            out += "|";
        } else {
            out += " " + debug_string(*chan.memory) + " ";
        }

        out += fmt::format("{}b", 8 * chan.line_stride / i.width);
        if (chan.memory_offset) out += "@" + debug_size(chan.memory_offset);
    }

    if (i.channels.empty()) out += " [no data]";
    return out;
}

}  // namespace pivid

