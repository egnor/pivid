#pragma once

#include "file_descriptor.h"

namespace pivid {

struct FrameBuffer {
    struct Channel {
        std::shared_ptr<int const> dma_fd;
        int offset = 0;
        int line_pitch = 0;
    };

    uint32_t fourcc = 0;
    int width = 0;
    int height = 0;
    std::vector<Channel> channels;
};

}  // namespace pivid
