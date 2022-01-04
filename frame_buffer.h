#pragma once

#include <compare>

namespace pivid {

struct FrameBuffer {
    struct Channel {
        int dma_fd;
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
