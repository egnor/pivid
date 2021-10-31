// Simple command line tool to exercise h.264 decoding via V4L2

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cctype>
#include <chrono>
#include <deque>
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

DEFINE_int32(encoded_buffers, 16, "Buffered frames for decoder input");
DEFINE_int32(decoded_buffers, 16, "Buffered frames for decoder output");
DEFINE_bool(print_io, false, "Print individual buffer operations");

struct InputMedia {
    AVFormatContext* avc = nullptr;
    AVStream* stream = nullptr;
    AVBSFContext* filter = nullptr;
    AVPacket* packet = nullptr;

    InputMedia() {}
    ~InputMedia() {
        if (packet) av_packet_free(&packet);
        // if (filter) av_bsf_free(&filter);  // Seems to cause double-free??
        if (avc) avformat_close_input(&avc);
    }

    InputMedia(InputMedia const&) = delete;
    void operator=(InputMedia const&) = delete;
};

struct OutputContext {
    std::string prefix;
    AVCodec const* codec = nullptr;
    AVCodecContext* context = nullptr;
    AVPacket* packet = nullptr;

    OutputContext() {}
    ~OutputContext() {
        if (packet) av_packet_free(&packet);
        if (context) avcodec_free_context(&context);
    }
};

struct MappedBuffer {
    v4l2_buffer buffer = {};
    v4l2_plane plane = {};
    void *mmap = MAP_FAILED;
    size_t size = 0;

    MappedBuffer() {}
    ~MappedBuffer() { if (mmap != MAP_FAILED) munmap(mmap, size); }

    MappedBuffer(MappedBuffer const&) = delete;
    void operator=(MappedBuffer const&) = delete;
};

// Updates input.packet with the next packet from filtered input.
// At EOF, the packet will be empty (!input.packet->buf).
void input_next_packet(InputMedia const& input) {
    for (;;) {
        av_packet_unref(input.packet);
        int const filt_err = av_bsf_receive_packet(input.filter, input.packet);
        if (filt_err >= 0 || filt_err == AVERROR_EOF) return;
        if (filt_err != AVERROR(EAGAIN)) {
            fmt::print("*** Input filter: {}\n", strerror(errno));
            exit(1);
        }

        do {
            av_packet_unref(input.packet);
            int const codec_err = av_read_frame(input.avc, input.packet);
            if (codec_err == AVERROR_EOF) return;
            if (codec_err < 0) {
                fmt::print("*** Reading input: {}\n", strerror(errno));
                exit(1);
            }
        } while (input.packet->stream_index != input.stream->index);

        av_bsf_send_packet(input.filter, input.packet);
    }
}

// Opens media file with libavformat and finds H.264 stream
std::unique_ptr<InputMedia> open_input(const std::string& url) {
    auto input = std::make_unique<InputMedia>();
    if (avformat_open_input(&input->avc, url.c_str(), nullptr, nullptr) < 0) {
        fmt::print("*** {}: {}\n", url, strerror(errno));
        exit(1);
    }

    if (avformat_find_stream_info(input->avc, nullptr) < 0) {
        fmt::print("*** Stream info ({}): {}\n", url, strerror(errno));
        exit(1);
    }

    fmt::print("Media file: {}\n", url);

    for (uint32_t si = 0; si < input->avc->nb_streams; ++si) {
        auto* maybe = input->avc->streams[si];
        if (maybe->codecpar && maybe->codecpar->codec_id == AV_CODEC_ID_H264) {
            input->stream = maybe;
            break;
        }
    }

    if (!input->stream) {
        fmt::print("*** {}: No H.264 stream found\n", url);
        exit(1);
    }

    fmt::print(
        "H.264 stream (#{}): {}x{} {:.1f}fps\n",
        input->stream->index,
        input->stream->codecpar->width, input->stream->codecpar->height,
        av_q2d(input->stream->avg_frame_rate)
    );

    auto const* const filter_type = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter_type) {
        fmt::print("*** No \"h264_mp4toannexb\" bitstream filter available\n");
        exit(1);
    }

    if (av_bsf_alloc(filter_type, &input->filter) < 0) {
        fmt::print("*** MP4-to-AnnexB filter alloc: {}\n", strerror(errno));
        exit(1);
    }

    input->filter->par_in = input->stream->codecpar;
    input->filter->time_base_in = input->stream->time_base;
    if (av_bsf_init(input->filter) < 0) {
        fmt::print("*** MP4-to-AnnexB filter init: {}\n", strerror(errno));
        exit(1);
    }

    input->packet = av_packet_alloc();
    input_next_packet(*input);
    return input;
}

// Scan V4L2 devices and find H.264 decoder
int open_decoder() {
    std::filesystem::path const dev_dir = "/dev";
    for (const auto& entry : std::filesystem::directory_iterator(dev_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 5) != "video" || !isdigit(filename[5])) {
            continue;
        }

        auto const& native = entry.path().native();
        int const fd = open(native.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            fmt::print("*** {}: {}\n", native, strerror(errno));
            continue;
        }
        if (v4l2_fd_open(fd, V4L2_DISABLE_CONVERSION) != fd) {
            fmt::print("*** V4L2 ({}): {}\n", native, strerror(errno));
            close(fd);
            continue;
        }

        // We don't use MPLANE support but it's what the Pi decoder has.
        // To be more general we'd check for non-MPLANE VIDEO_M2M also.
        v4l2_fmtdesc format = {};
        v4l2_capability cap = {};
        if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
            fmt::print("*** Querying ({})\n", native, strerror(errno));
        } else if (
            (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) &&
            (cap.capabilities & V4L2_CAP_STREAMING)
        ) {
            for (
                format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                !v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &format);
                ++format.index
            ) {
               if (format.pixelformat == V4L2_PIX_FMT_H264) {
                   fmt::print("H.264 decoder: {}\n", native);
                   return fd;
               }
            }
        }

        v4l2_close(fd);
    }

    fmt::print("*** No V4L2 H.264 decoder device found\n");
    exit(1);
}

std::string describe(v4l2_buf_type type) {
    switch (type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE: return "dec";
        case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE: return "enc";
        default: return fmt::format("?type={}?", type);
    }
}

std::string describe(v4l2_buffer const& buf) {
    std::string out = fmt::format(
        "{} buf #{:<2d} t={:03d}.{:03d} {:4d}/{:4d}kB",
        describe((v4l2_buf_type) buf.type), buf.index,
        buf.timestamp.tv_sec, buf.timestamp.tv_usec / 1000,
        buf.m.planes[0].bytesused / 1024, buf.m.planes[0].length / 1024
    );
    switch (buf.memory) {
#define M(X) case V4L2_MEMORY_##X: out += fmt::format(" {}", #X); break
        M(MMAP);
        M(USERPTR);
        M(DMABUF);
#undef M
        default: out += fmt::format(" ?mem={}", buf.memory); break;
    }
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
    return out;
}

std::string describe(v4l2_format const& format, int num_buffers) {
    std::string out = describe((v4l2_buf_type) format.type);
    auto const& pix = format.fmt.pix_mp;
    if (pix.width || pix.height) {
        out += fmt::format(" {}x{}", pix.width, pix.height);
    }
    out += fmt::format(" {:.4s}", (char const*) &pix.pixelformat);
    out += fmt::format(" {} x (", num_buffers);
    for (int pi = 0; pi < pix.num_planes; ++pi) {
        out += fmt::format(
            "{}{}kB", (pi ? " + " : ""), pix.plane_fmt[pi].sizeimage / 1024
        );
    }
    return out + fmt::format(") buf(s)");
}

std::vector<std::unique_ptr<MappedBuffer>> decoder_mmap_buffers(
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

void decoder_setup_events(int const fd) {
    v4l2_event_subscription subscribe = {};
    subscribe.type = V4L2_EVENT_SOURCE_CHANGE;
    if (v4l2_ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &subscribe)) {
        fmt::print("*** Subscribing to SOURCE_CHANGE: {}\n", strerror(errno));
        exit(1);
    }

    subscribe.type = V4L2_EVENT_EOS;
    if (v4l2_ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &subscribe)) {
        fmt::print("*** Subscribing to EOS: {}\n", strerror(errno));
        exit(1);
    }
}

// "OUTPUT" (app-to-device, i.e. decoder *input*) setup
void decoder_setup_encoded_stream(
    int const fd, // int const width, int const height,
    std::vector<std::unique_ptr<MappedBuffer>>* const buffers
) {
    if (!buffers->empty()) {
        fmt::print("*** Encoded stream buffers reconfigured?!?!\n");
        exit(1);
    }

    v4l2_format encoded_format = {};
    encoded_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    // encoded_format.fmt.pix_mp.width = width;
    // encoded_format.fmt.pix_mp.height = height;
    encoded_format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    encoded_format.fmt.pix_mp.num_planes = 1;
    if (v4l2_ioctl(fd, VIDIOC_S_FMT, &encoded_format)) {
        fmt::print("*** Setting encoded format: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers encoded_reqbuf = {};
    encoded_reqbuf.type = encoded_format.type;
    encoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &encoded_reqbuf)) {
        fmt::print("*** Freeing encoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    encoded_reqbuf.count = FLAGS_encoded_buffers;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &encoded_reqbuf)) {
        fmt::print("*** Creating encoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &encoded_format.type)) {
        fmt::print("*** Starting encoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = decoder_mmap_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    fmt::print("ON: {}\n", describe(encoded_format, encoded_reqbuf.count));
}

// "CAPTURE" (device-to-app, i.e. decoder *output*) setup
void decoder_setup_decoded_stream(
    int const fd,
    std::vector<std::unique_ptr<MappedBuffer>>* const buffers
) {
    buffers->clear();  // Unmap existing buffers, if any.

    v4l2_format decoded_format = {};
    decoded_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (v4l2_ioctl(fd, VIDIOC_G_FMT, &decoded_format)) {
        fmt::print("*** Getting decoded format: {}\n", strerror(errno));
        exit(1);
    }

    if (v4l2_ioctl(fd, VIDIOC_STREAMOFF, &decoded_format.type)) {
        fmt::print("*** Stopping decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers decoded_reqbuf = {};
    decoded_reqbuf.type = decoded_format.type;
    decoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Freeing decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    decoded_reqbuf.count = FLAGS_decoded_buffers;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Creating decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &decoded_format.type)) {
        fmt::print("*** Starting decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = decoder_mmap_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    fmt::print("ON: {}\n", describe(decoded_format, decoded_reqbuf.count));

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

void decoder_run(InputMedia const& input, int const fd) {
    if (!input.packet->buf) {
        fmt::print("*** No packets in input stream #{}\n", input.stream->index);
        exit(1);
    }

    std::vector<std::unique_ptr<MappedBuffer>> encoded_buffers;
    std::vector<std::unique_ptr<MappedBuffer>> decoded_buffers;

    decoder_setup_encoded_stream(fd, &encoded_buffers);
    decoder_setup_decoded_stream(fd, &decoded_buffers);

    std::deque<MappedBuffer*> encoded_free, decoded_free;
    for (auto const &b : encoded_buffers) encoded_free.push_back(b.get());
    for (auto const &b : decoded_buffers) decoded_free.push_back(b.get());

    auto const encoded_buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    auto const decoded_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    auto const time_base = av_q2d(input.stream->time_base);

    v4l2_plane received_plane = {};
    v4l2_buffer received = {};
    received.memory = V4L2_MEMORY_MMAP;
    received.length = 1;
    received.m.planes = &received_plane;

    int frame_count = 0;
    time_t frame_sec = 0;
    auto const start_t = std::chrono::steady_clock::now();

    bool decoded_format_changed = false;
    bool decoded_done = false;
    while (
        !decoded_done || input.packet->buf ||
        encoded_free.size() < encoded_buffers.size()
    ) {
        // Reclaim encoded buffers once consumed by the decoder.
        received.type = encoded_buf_type;
        while (!v4l2_ioctl(fd, VIDIOC_DQBUF, &received)) {
            if (received.index > encoded_buffers.size()) {
                fmt::print("*** Bad reclaimed index: #{}\n", received.index);
                exit(1);
            }
            if (FLAGS_print_io) {
                fmt::print("Reclaimed {}\n", describe(received));
            }
            encoded_free.push_back(encoded_buffers[received.index].get());
        }
        if (errno != EAGAIN) {
            fmt::print("*** Reclaiming encoded buffer: {}\n", strerror(errno));
            exit(1);
        }

        // Push encoded data into the decoder.
        while (!encoded_free.empty() && input.packet->buf) {
            auto* send = encoded_free.front();
            encoded_free.pop_front();
            if (input.packet->size > (int) send->size) {
                fmt::print(
                    "*** Coded packet ({}kB) too big for buffer ({}kB)\n",
                    input.packet->size / 1024, send->size / 1024
                );
                exit(1);
            }

            send->plane.bytesused = 0;
            do {
                if (input.packet->pts == AV_NOPTS_VALUE) {
                    send->buffer.timestamp = {};
                } else {
                    double const time = input.packet->pts * time_base;
                    uint32_t const sec = time;
                    send->buffer.timestamp.tv_sec = sec;
                    send->buffer.timestamp.tv_usec = (time - sec) * 1000000;
                }

                memcpy(
                    ((uint8_t *) send->mmap) + send->plane.bytesused,
                    input.packet->data, input.packet->size
                );
                send->plane.bytesused += input.packet->size;
                input_next_packet(input);
            } while (
                input.packet->buf &&
                send->plane.bytesused + input.packet->size <= send->size
            );

            if (v4l2_ioctl(fd, VIDIOC_QBUF, &send->buffer)) {
                fmt::print("*** Sending encoded buffer: {}\n", strerror(errno));
                exit(1);
            }
            if (FLAGS_print_io) {
                fmt::print("Sent      {}\n", describe(send->buffer));
            }

            if (!input.packet->buf) {
                v4l2_decoder_cmd command = {};
                command.cmd = V4L2_DEC_CMD_STOP;
                if (v4l2_ioctl(fd, VIDIOC_DECODER_CMD, &command)) {
                    fmt::print("*** Sending STOP: {}\n", strerror(errno));
                    exit(1);
                }
                fmt::print("--- Encoded data is fully sent, sent STOP\n");
            }
        }

        // Send free decoded buffers to be filled by the decoder.
        bool decoded_buffer_invalid = false;
        while (
            !decoded_format_changed && !decoded_buffer_invalid &&
            !decoded_done && !decoded_free.empty()
        ) {
            auto* recycle = decoded_free.front();
            decoded_free.pop_front();
            recycle->plane.bytesused = 0;
            recycle->buffer.flags |= V4L2_BUF_FLAG_NO_CACHE_CLEAN;
            recycle->buffer.flags |= V4L2_BUF_FLAG_NO_CACHE_INVALIDATE;
            if (v4l2_ioctl(fd, VIDIOC_QBUF, &recycle->buffer)) {
                if (errno == EINVAL) {
                    decoded_buffer_invalid = true;  // Maybe source change.
                    decoded_free.push_front(recycle);  // Put it back!
                } else {
                    fmt::print("*** Recycling buffer: {}\n", strerror(errno));
                    exit(1);
                }
            } else if (FLAGS_print_io) {
                fmt::print("Recycled  {}\n", describe(recycle->buffer));
            }
        }

        // Receive decoded data and add the buffers to the free list.
        received.type = decoded_buf_type;
        while (!decoded_done && !v4l2_ioctl(fd, VIDIOC_DQBUF, &received)) {
            if (received.index > decoded_buffers.size()) {
                fmt::print("*** Bad decoded index: #{}\n", received.index);
                exit(1);
            }

            auto* decoded = decoded_buffers[received.index].get();
            if (FLAGS_print_io) {
                fmt::print("Received  {}\n", describe(received));
            }
            if (received.flags & V4L2_BUF_FLAG_LAST) {
                fmt::print("--- LAST flag on decoded stream (drained)\n");
                decoded_done = true;
            }

            ++frame_count;
            if (received.timestamp.tv_sec > frame_sec) {
                frame_sec = received.timestamp.tv_sec;
                double const elapsed_t = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_t).count();
                fmt::print(
                    "::: {}fr decoded / {:.1f}s elapsed = {:.2f} fps\n",
                    frame_count, elapsed_t, frame_count / elapsed_t);
            }

            //
            //
            //

            decoded_free.push_back(decoded);
        }
        if (errno == EPIPE) {
            fmt::print("--- EPIPE from decoded stream (assume drained)\n");
            decoded_done = true;  // No buffer to mark with V4L2_BUF_FLAG_LAST?
        } else if (errno != EAGAIN) {
            fmt::print("*** Receiving decoded buffer: {}\n", strerror(errno));
            exit(1);
        }

        // Receive events. This must happen AFTER decoded-stream DQBUF, so we
        // don't get spurious end-of-stream markers due to source changes.
        v4l2_event event = {};
        while (!v4l2_ioctl(fd, VIDIOC_DQEVENT, &event)) {
            if (event.type == V4L2_EVENT_SOURCE_CHANGE) {
                fmt::print("--- SOURCE_CHANGE event (will reconfigure)\n");
                decoded_format_changed = true;
            } else if (event.type == V4L2_EVENT_EOS) {
                // Don't take action on this (legacy) event, but log it FYI.
                fmt::print("--- EOS event (encoded data is processed FYI)\n");
            } else {
                fmt::print("*** Unknown event ?type={}?\n", event.type);
            }
        }
        if (errno != ENOENT) {
            fmt::print("*** Receiving event: {}\n", strerror(errno));
            exit(1);
        }

        if (decoded_buffer_invalid && !decoded_format_changed) {
            fmt::print(
                "*** Recycling decoded buffer (no SOURCE_CHANGE): {}\n",
                strerror(EINVAL)
            );
        }

        if (decoded_done && decoded_format_changed) {
            fmt::print("--- Reconfiguring after SOURCE_CHANGE\n");
            decoded_done = decoded_format_changed = false;  // Restart
            decoded_free.clear();
            decoder_setup_decoded_stream(fd, &decoded_buffers);
            for (auto const &b : decoded_buffers)
                decoded_free.push_back(b.get());
        }

        // Wait 10ms before polling again -- TODO use poll() instead?
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

DEFINE_string(input, "", "Media file or URL to decode");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_input.empty()) {
        fmt::print("*** Usage: pivid_test_decode --input=<mediafile>\n");
        exit(1);
    }

    auto const input = open_input(FLAGS_input);
    int const decoder_fd = open_decoder();
    decoder_setup_events(decoder_fd);
    decoder_run(*input, decoder_fd);
    v4l2_close(decoder_fd);

    fmt::print("Closed and complete!\n");
    return 0;
}
