#include "image_buffer.h"

#include <drm_fourcc.h>

#include <fmt/core.h>

namespace pivid {

std::string debug_size(size_t s) {
    if (s < 1000) return fmt::format("{}B", s);
    if (s < 10240) return fmt::format("{:.1f}K", s / 1024.0);
    if (s < 1024000) return fmt::format("{}K", s / 1024);
    if (s < 10485760) return fmt::format("{:.1f}M", s / 1048576.0);
    if (s < 1048576000) return fmt::format("{}M", s / 1048576);
    return fmt::format("{:.1f}G", s / 1073741824.0);
}

std::string debug_fourcc(uint32_t fourcc) {
    std::string out;
    for (int i = 0; i < 4; ++i) {
        int const ch = (fourcc >> (i * 8)) & 0xFF;
        if (ch > 32) out.append(1, ch);
        if (ch > 0 && ch < 32) out.append(fmt::format("{}", ch));
    }
    return out;
}

std::string debug(MemoryBuffer const& mem) {
    std::string out = debug_size(mem.size());
    if (mem.dma_fd() >= 0) out += fmt::format(" f{:<2d}", mem.dma_fd());
    if (mem.drm_handle()) out += fmt::format(" h{:<2d}", mem.drm_handle());
    return out;
}

std::string debug(ImageBuffer const& i) {
    std::string out = fmt::format(
        "{}x{} {}", i.size.x, i.size.y, debug_fourcc(i.fourcc)
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
        if (c == 0) {
            out += " " + debug(*chan.memory) + ":";
        } else if (chan.memory == i.channels[c - 1].memory) {
            out += ",";
        } else {
            out += "|" + debug(*chan.memory) + ":";
        }

        out += fmt::format("{}b", 8 * chan.stride / i.size.x);
        if (chan.offset) out += fmt::format("@{}", debug_size(chan.offset));
    }

    if (i.channels.empty()) out += " [no data]";
    if (!i.source_comment.empty())
        out += fmt::format(" \"{}\"", i.source_comment);

    return out;
}

std::string debug(LoadedImage const& l) {
    auto const size = l.size();
    auto const comment = l.source_comment();
    auto out = fmt::format("{}x{} fb{}", size.x, size.y, l.drm_id());
    if (!comment.empty())
        out += fmt::format(" \"{}\"", comment);
    return out;
}

}  // namespace pivid
