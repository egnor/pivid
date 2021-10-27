// Simple command line tool to exercise h.264 decoding via V4L2

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>  // just for sleep_for(), don't panic
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>
#include <libv4l2.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
}

DEFINE_int32(coded_buffers, 16, "Buffered frames for decoder input");
DEFINE_int32(decoded_buffers, 16, "Buffered frames for decoder output");

struct MappedBuffer {
    v4l2_buffer buffer = {};
    v4l2_plane plane = {};
    void *mmap = MAP_FAILED;
    size_t size = 0;

    ~MappedBuffer() { if (mmap != MAP_FAILED) munmap(mmap, size); }
};

// Open media file with libavformat and find H.264 stream
void open_media_h264_stream(
    const std::string& filename,
    AVFormatContext** context,
    AVStream** stream
) {
    *context = nullptr;
    if (avformat_open_input(context, filename.c_str(), nullptr, nullptr) < 0) {
        fmt::print("*** {}: {}\n", filename, strerror(errno));
        exit(1);
    }

    if (avformat_find_stream_info(*context, nullptr) < 0) {
        fmt::print("*** Stream info ({}): {}\n", filename, strerror(errno));
        exit(1);
    }

    fmt::print("Media file: {}\n", filename);

    *stream = nullptr;
    for (uint32_t si = 0; si < (*context)->nb_streams; ++si) {
        auto* maybe = (*context)->streams[si];
        if (maybe->codecpar && maybe->codecpar->codec_id == AV_CODEC_ID_H264) {
            *stream = maybe;
            break;
        }
    }

    if (!*stream) {
        fmt::print("*** {}: No H.264 stream found\n", filename);
        exit(1);
    }

    fmt::print(
        "H.264 stream (#{}): {}x{} {:.1f}fps\n",
        (*stream)->index,
        (*stream)->codecpar->width, (*stream)->codecpar->height,
        av_q2d((*stream)->avg_frame_rate)
    );
}

// Scan V4L2 devices and find H.264 decoder
void open_decoder(int* fd) {
    std::filesystem::path const dev_dir = "/dev";
    for (const auto& entry : std::filesystem::directory_iterator(dev_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 5) != "video" || !isdigit(filename[5])) {
            continue;
        }

        auto const& native = entry.path().native();
        *fd = open(native.c_str(), O_RDWR | O_NONBLOCK);
        if (*fd < 0) {
            fmt::print("*** {}: {}\n", native, strerror(errno));
            continue;
        }
        if (v4l2_fd_open(*fd, V4L2_DISABLE_CONVERSION) != *fd) {
            fmt::print("*** V4L2 ({}): {}\n", native, strerror(errno));
            close(*fd);
            continue;
        }

        // We don't use MPLANE support but it's what the Pi decoder has.
        // To be more general we'd check for non-MPLANE VIDEO_M2M also.
        v4l2_fmtdesc format = {};
        v4l2_capability cap = {};
        if (v4l2_ioctl(*fd, VIDIOC_QUERYCAP, &cap)) {
            fmt::print("*** Querying ({})\n", native, strerror(errno));
        } else if (
            (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) &&
            (cap.capabilities & V4L2_CAP_STREAMING)
        ) {
            for (
                format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                v4l2_ioctl(*fd, VIDIOC_ENUM_FMT, &format) >= 0;
                ++format.index
            ) {
               if (format.pixelformat == V4L2_PIX_FMT_H264) {
                   fmt::print("H.264 decoder: {}\n", native);
                   return;
               }
            }
        }

        v4l2_close(*fd);
    }

    fmt::print("*** No V4L2 H.264 decoder device found\n");
    exit(1);
}

void stop_decoder(int const fd) {
    int const coded_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (v4l2_ioctl(fd, VIDIOC_STREAMOFF, &coded_type)) {
        fmt::print("*** Stopping coded stream: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers coded_reqbuf = {};
    coded_reqbuf.type = coded_type;
    coded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &coded_reqbuf)) {
        fmt::print("*** Releasing coded buffers: {}\n", strerror(errno));
        exit(1);
    }
    fmt::print("Coded stream OFF (buffers released)\n");

    int const decoded_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (v4l2_ioctl(fd, VIDIOC_STREAMOFF, &decoded_type)) {
        fmt::print("*** Stopping decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers decoded_reqbuf = {};
    decoded_reqbuf.type = decoded_type;
    decoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Releasing decoded buffers: {}\n", strerror(errno));
        exit(1);
    }
    fmt::print("Decoded stream OFF (buffers released)\n");
}

void setup_decoder(int const fd, int width, int height) {
    // https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-decoder.html

    //
    // "OUTPUT" (app-to-device, i.e. decoder *input*) setup
    //

    v4l2_format coded_format = {};
    coded_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    coded_format.fmt.pix_mp.width = width;
    coded_format.fmt.pix_mp.height = height;
    coded_format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    coded_format.fmt.pix_mp.num_planes = 1;
    if (v4l2_ioctl(fd, VIDIOC_S_FMT, &coded_format)) {
        fmt::print("*** Setting coded format: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers coded_reqbuf = {};
    coded_reqbuf.count = FLAGS_coded_buffers;
    coded_reqbuf.type = coded_format.type;
    coded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &coded_reqbuf)) {
        fmt::print("*** Creating coded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &coded_format.type)) {
        fmt::print("*** Starting coded stream: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print(
        "Coded stream ON:   {}x{} {:.4s} {} buf(s) x {} plane(s) x {}kB\n",
        (int) coded_format.fmt.pix_mp.width,
        (int) coded_format.fmt.pix_mp.height,
        (char const*) &coded_format.fmt.pix_mp.pixelformat,
        coded_reqbuf.count,
        coded_format.fmt.pix_mp.num_planes,
        coded_format.fmt.pix_mp.plane_fmt[0].sizeimage / 1024
    );

    // Not sending stream data for setup -- hopefully not needed?

    //
    // "CAPTURE" (device-to-app, i.e. decoder *output*) setup
    //

    v4l2_format decoded_format = {};
    decoded_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (v4l2_ioctl(fd, VIDIOC_G_FMT, &decoded_format)) {
        fmt::print("*** Getting decoded format: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers decoded_reqbuf = {};
    decoded_reqbuf.count = FLAGS_decoded_buffers;
    decoded_reqbuf.type = decoded_format.type;
    decoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Creating decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &decoded_format.type)) {
        fmt::print("*** Starting decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print(
        "Decoded stream ON: {}x{} {:.4s} {} buf(s) x {} plane(s) x {}kB\n",
        (int) decoded_format.fmt.pix_mp.width,
        (int) decoded_format.fmt.pix_mp.height,
        (char const*) &decoded_format.fmt.pix_mp.pixelformat,
        decoded_reqbuf.count,
        decoded_format.fmt.pix_mp.num_planes,
        decoded_format.fmt.pix_mp.plane_fmt[0].sizeimage / 1024
    );

    for (int const target : {0x0, 0x1, 0x2, 0x3, 0x100, 0x101, 0x102, 0x103}) {
        v4l2_selection selection = {};
        selection.type = decoded_format.type;
        selection.target = target;
        if (!v4l2_ioctl(fd, VIDIOC_G_SELECTION, &selection)) {
            switch (target) {
#define T(X) case V4L2_SEL_TGT_##X: fmt::print("    {:15}", #X); break
                T(CROP);
                T(CROP_DEFAULT);
                T(CROP_BOUNDS);
                T(NATIVE_SIZE);
                T(COMPOSE);
                T(COMPOSE_DEFAULT);
                T(COMPOSE_BOUNDS);
                T(COMPOSE_PADDED);
#undef T
                default: fmt::print("    ?0x{:x}?", target); break;
            }
            fmt::print(
                " ({},{})+({}x{})\n",
                selection.r.left, selection.r.top,
                selection.r.width, selection.r.height
            );
        }
    }
}

std::string describe(v4l2_buffer const& buf) {
    std::string out;
    switch (buf.type) {
#define T(X, y) case V4L2_BUF_TYPE_##X: out += fmt::format("{}", y); break
        T(VIDEO_CAPTURE_MPLANE, "decoded");
        T(VIDEO_OUTPUT_MPLANE, "coded");
#undef T
        default: out += fmt::format("?type={}?", buf.type); break;
    }
    out += fmt::format(
        " buffer #{} {}/{}kB", buf.index,
        buf.m.planes[0].bytesused / 1024, buf.m.planes[0].length / 1024
    );
    for (uint32_t bit = 1; bit; bit <<= 1) {
        if (buf.flags & bit) {
            switch (bit) {
#define F(X) case V4L2_BUF_FLAG_##X: out += fmt::format(" {}", #X); break
                F(MAPPED);
                F(QUEUED);
                F(DONE);
                F(ERROR);
                F(IN_REQUEST);
                F(KEYFRAME);
                F(PFRAME);
                F(BFRAME);
                F(TIMECODE);
                F(PREPARED);
                F(NO_CACHE_INVALIDATE);
                F(NO_CACHE_CLEAN);
                F(M2M_HOLD_CAPTURE_BUF);
                F(LAST);
                F(REQUEST_FD);
                F(TIMESTAMP_MONOTONIC);
                F(TIMESTAMP_COPY);
                F(TSTAMP_SRC_MASK);
                F(TSTAMP_SRC_SOE);
#undef F
                default: out += fmt::format(" ?flag=0x{:x}?", bit); break;
            }
        }
    }
    switch (buf.memory) {
#define M(X) case V4L2_MEMORY_##X: out += fmt::format(" {}", #X); break
        M(MMAP);
        M(USERPTR);
        M(DMABUF);
#undef M
        default: out += fmt::format(" ?mem={}", buf.memory); break;
    }
    return out;
}

std::vector<std::unique_ptr<MappedBuffer>> map_decoder_buffers(
    int const fd, v4l2_buf_type const type
) {
    std::vector<std::unique_ptr<MappedBuffer>> buffers;

    for (int bi = 0;; ++bi) {
        auto mapped = std::make_unique<MappedBuffer>();
        mapped->buffer.type = type;
        mapped->buffer.index = bi;
        mapped->buffer.length = 1;
        mapped->buffer.m.planes = &mapped->plane;

        if (v4l2_ioctl(fd, VIDIOC_QUERYBUF, &mapped->buffer)) {
            if (bi > 0 && errno == EINVAL) break;
            fmt::print(
                "*** Querying {}: {}\n",
                describe(mapped->buffer), strerror(errno)
            );
            exit(1);
        }

        mapped->size = mapped->plane.length;
        mapped->mmap = mmap(
            nullptr, mapped->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
            mapped->plane.m.mem_offset
        );
        if (mapped->mmap == MAP_FAILED) {
           fmt::print(
               "*** Memory mapping {} (@{} +{}b): {}\n",
               describe(mapped->buffer),
               mapped->plane.m.mem_offset, mapped->plane.length, strerror(errno)
           );
           exit(1);
        }
        buffers.push_back(std::move(mapped));
    }
    return buffers;
}

void run_decoder(AVFormatContext* context, AVStream* stream, int const fd) {
    auto const coded_buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    auto const decoded_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    auto const coded_buffers = map_decoder_buffers(fd, coded_buf_type);
    auto const decoded_buffers = map_decoder_buffers(fd, decoded_buf_type);
    fmt::print(
        "Buffers mapped: coded={}x{}kB, decoded={}x{}kB\n",
        coded_buffers.size(), coded_buffers[0]->size / 1024,
        decoded_buffers.size(), decoded_buffers[0]->size / 1024
    );

    std::vector<MappedBuffer*> coded_free, decoded_free;
    for (auto const &b : coded_buffers) coded_free.push_back(b.get());
    for (auto const &b : decoded_buffers) decoded_free.push_back(b.get());

    bool packet_ok = false;
    AVPacket packet = {};
    while (!packet_ok && av_read_frame(context, &packet) >= 0) {
        packet_ok = (packet.stream_index == stream->index);
    }
    if (!packet_ok) {
        fmt::print("*** No coded packets in stream #{}\n", stream->index);
        exit(1);
    }

    v4l2_plane received_plane = {};
    v4l2_buffer received = {};
    received.memory = V4L2_MEMORY_MMAP;
    received.length = 1;
    received.m.planes = &received_plane;

    bool drained = false;

    v4l2_decoder_cmd command = {};
    command.cmd = V4L2_DEC_CMD_START;
    if (v4l2_ioctl(fd, VIDIOC_DECODER_CMD, &command)) {
        fmt::print("*** Sending START: {}\n", strerror(errno));
        exit(1);
    }
    fmt::print("Sent START\n");

    while (!drained) {
        // Reclaim coded buffers once consumed by the decoder.
        received.type = coded_buf_type;
        while (!v4l2_ioctl(fd, VIDIOC_DQBUF, &received)) {
            if (received.index > coded_buffers.size()) {
                fmt::print("*** Bad reclaimed index: #{}\n", received.index);
                exit(1);
            }
            fmt::print("Reclaimed {}\n", describe(received));
            coded_free.push_back(coded_buffers[received.index].get());
        }
        if (errno != EAGAIN) {
            fmt::print("*** Reclaiming coded buffer: {}\n", strerror(errno));
            exit(1);
        }

        // Push coded data into the decoder.
        while (!coded_free.empty() && packet_ok) {
            auto* coded = coded_free.back();
            coded_free.pop_back();
            coded->plane.bytesused = 0;

            do {
                if (coded->plane.bytesused + packet.size >= coded->size) {
                    fmt::print(
                        "*** Coded packet ({}kB) too big for buffer ({}kB)\n",
                        packet.size / 1024, coded->size / 1024
                    );
                    exit(1);
                }

                memcpy(
                    ((uint8_t *) coded->mmap) + coded->plane.bytesused,
                    packet.data, packet.size
                );
                coded->plane.bytesused += packet.size;
                packet_ok = false;
                while (!packet_ok && av_read_frame(context, &packet) >= 0) {
                    packet_ok = (packet.stream_index == stream->index);
                }
            } while (
                packet_ok &&
                coded->plane.bytesused + packet.size <= coded->size
            );

            if (v4l2_ioctl(fd, VIDIOC_QBUF, &coded->buffer)) {
                fmt::print("*** Sending coded buffer: {}\n", strerror(errno));
                exit(1);
            }
            fmt::print("Sent {}\n", describe(coded->buffer));

            if (!packet_ok) {
                v4l2_decoder_cmd command = {};
                command.cmd = V4L2_DEC_CMD_STOP;
                if (v4l2_ioctl(fd, VIDIOC_DECODER_CMD, &command)) {
                    fmt::print("*** Sending STOP: {}\n", strerror(errno));
                    exit(1);
                }
                fmt::print("Sent STOP\n");
            }
        }

        // Send empty decoded buffers to be filled by the decoder.
        while (!decoded_free.empty()) {
            auto* decoded = decoded_free.back();
            decoded_free.pop_back();
            if (v4l2_ioctl(fd, VIDIOC_QBUF, &decoded->buffer)) {
                fmt::print("*** Cycling buffer: {}\n", strerror(errno));
                exit(1);
            }
            fmt::print("Cycled {}\n", describe(decoded->buffer));
        }

        // Receive decoded data and return the buffers.
        received.type = decoded_buf_type;
        while (!drained && !v4l2_ioctl(fd, VIDIOC_DQBUF, &received)) {
            if (received.index > decoded_buffers.size()) {
                fmt::print("*** Bad decoded index: #{}\n", received.index);
                exit(1);
            }

            drained = (received.flags & V4L2_BUF_FLAG_LAST);
            auto* decoded = decoded_buffers[received.index].get();
            fmt::print("Received {}\n", describe(received));
            //
            //
            //

            decoded_free.push_back(decoded);
        }
        if (errno != EAGAIN) {
            fmt::print("*** Receiving decoded buffer: {}\n", strerror(errno));
            exit(1);
        }

        // Poll after a 10ms delay -- TODO use poll() instead
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    (void) context;
    (void) stream;
}

void decode_media(const std::string& filename) {
    AVFormatContext* av_context = nullptr;
    AVStream* av_stream = nullptr;
    open_media_h264_stream(filename, &av_context, &av_stream);
    auto const* av_codec = av_stream->codecpar;

    int decoder_fd = -1;
    open_decoder(&decoder_fd);
    setup_decoder(decoder_fd, av_codec->width, av_codec->height);
    run_decoder(av_context, av_stream, decoder_fd);
    stop_decoder(decoder_fd);
    v4l2_close(decoder_fd);
    avformat_close_input(&av_context);
    fmt::print("Decoder and media file CLOSED\n");
}

DEFINE_string(media, "", "Media file or URL to decode");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_media.empty()) {
        fmt::print("*** Usage: pivid_test_decode --media=<mediafile>\n");
        exit(1);
    }

    decode_media(FLAGS_media);
    return 0;
}
