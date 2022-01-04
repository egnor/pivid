#pragma once

#include <compare>

namespace pivid {

struct FrameBuffer {
    struct Channel {
        int dma_fd;
        int start_offset = 0;
        int bytes_per_line = 0;
        auto operator<=>(Channel const&) const = default;
    };

    std::vector<Channel> channels;
    uint32_t fourcc = 0;
    uint64_t modifier = 0;
    int width = 0;
    int height = 0;
    auto operator<=>(FrameBuffer const&) const = default;
};

}  // namespace pivid
