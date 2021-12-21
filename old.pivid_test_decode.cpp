// Simple command line tool to exercise H.264 decoding via V4L2.
// THIS IS NOT A GOOD VIDEO PLAYER, it is just a proof of concept.
//
// To play an H.264-encoded video file:
//   - Stop X and other display users (e.g. "systemctl stop lightdm")
//   - Run this program: build/pivid_test_decode --media=your_video_file.mp4
//   - Maybe it works?
//
// Use --help to see other flags.
//
// This program is mostly intended for the Raspberry Pi, though it might
// work on any machine with similar hardware. It will use the first active
// screen and its default ("preferred") video mode.
//
// High level approach:
// - use libavformat to get H.264 data from the container (.mp4, .mkv, etc)
// - send the H.264 to the V4L2 decoder device in memory-mapped buffers
// - get decoded frames back; "export" the buffers to DMA-BUF
// - "import" the DMA-BUF buffers as framebuffers in KMS
// - use KMS atomic mode switching to switch the framebuffer
//
// See README.md for links and notes.

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

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

// Command line options set in main()
bool flat_out = false;
double start_delay = 0.2;
int encoded_buffer_size = 0;
int encoded_buffer_count = 16;
int decoded_buffer_count = 16;
bool print_io = false;

// State associated with using libavformat to extract H.264 from a media file
struct InputFromFile {
    AVFormatContext* context = nullptr;
    AVStream* stream = nullptr;
    AVBSFContext* filter = nullptr;
    AVPacket* packet = nullptr;

    InputFromFile() {}
    ~InputFromFile() {
        if (packet) av_packet_free(&packet);
        // if (filter) av_bsf_free(&filter);  // Seems to double-free things??
        if (context) avformat_close_input(&context);
    }

    InputFromFile(InputFromFile const&) = delete;
    void operator=(InputFromFile const&) = delete;
};

// State associated with using libavcodec to generate JPEG files
// (if the --frame_prefix option is used to save every frame)
struct JpegState {
    std::string prefix;
    AVCodecContext* context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int count = 0;

    JpegState() {}
    ~JpegState() {
        if (packet) av_packet_unref(packet);
        if (frame) av_frame_free(&frame);
        if (context) avcodec_free_context(&context);
    }

    JpegState(JpegState const&) = delete;
    void operator=(JpegState const&) = delete;
};

// State associated with sending buffers to KMS for on-screen display
struct ScreenState {
    std::string device = "/dev/dri/card0";
    int fd = -1;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    uint32_t plane_id = 0;

    drmModeModeInfo mode_info = {};
    uint32_t mode_blob = 0;

    std::map<std::string, drmModePropertyPtr> prop_name;
    std::map<int, drmModePropertyPtr> prop_id;
    drmModeAtomicReqPtr atomic_req = nullptr;

    ScreenState() {}
    ~ScreenState() {
        if (atomic_req) drmModeAtomicFree(atomic_req);
        if (mode_blob) drmModeDestroyPropertyBlob(fd, mode_blob);
        for (auto const p : prop_id) drmModeFreeProperty(p.second);
        if (fd >= 0) close(fd);
    }

    ScreenState(ScreenState const&) = delete;
    void operator=(ScreenState const&) = delete;
};

// A single kernel memory buffer, used either for sending H.264 data to V4L2
// or for moving a decoded frame from V4L2 to KMS (or saving as a JPEG)
struct MappedBuffer {
    int index = 0;
    void *mmap = nullptr;
    size_t size = 0;
    int dmabuf_fd = -1;
    uint32_t drm_handle = 0;
    uint32_t drm_framebuffer = 0;

    MappedBuffer() {}
    ~MappedBuffer() {
        if (mmap) munmap(mmap, size);
        if (dmabuf_fd >= 0) close(dmabuf_fd);
    }

    MappedBuffer(MappedBuffer const&) = delete;
    void operator=(MappedBuffer const&) = delete;
};

// Utility to print error messages for libavformat / libavcodec functions
void avcheck(int averr, std::string const& text) {
    if (averr < 0) {
        char buf[AV_ERROR_MAX_STRING_SIZE];
        av_make_error_string(buf, sizeof(buf), averr);
        fmt::print("*** {}: {}\n", text, buf);
        exit(1);
    }
}

// Updates input.packet with the next packet from filtered input.
// At EOF, the packet will be empty (!input.packet->buf).
void next_input_packet(InputFromFile const& input) {
    // Packets are read from libavformat and sent to the "bitstream filter"
    // the changes their headers as needed for V4L2.
    for (;;) {
        // Read anything pending from the bitstream filter.
        av_packet_unref(input.packet);
        int const filt_err = av_bsf_receive_packet(input.filter, input.packet);
        if (filt_err >= 0 || filt_err == AVERROR_EOF) return;
        if (filt_err != AVERROR(EAGAIN)) avcheck(filt_err, "Input filter");

        // If the bitstream filter is empty, feed it more input data,
        // skipping anything that isn't from the stream with H.264 data.
        do {
            av_packet_unref(input.packet);
            int const codec_err = av_read_frame(input.context, input.packet);
            if (codec_err == AVERROR_EOF) return;
            if (codec_err < 0) avcheck(codec_err, "Reading input");
        } while (input.packet->stream_index != input.stream->index);

        av_bsf_send_packet(input.filter, input.packet);
    }
}

// Opens a media file with libavformat and finds the H.264 stream.
std::unique_ptr<InputFromFile> open_input(const std::string& url) {
    auto in = std::make_unique<InputFromFile>();
    avcheck(
        avformat_open_input(&in->context, url.c_str(), nullptr, nullptr),
        url
    );

    avcheck(avformat_find_stream_info(in->context, nullptr), "Stream info");

    fmt::print("Media file: {}\n", url);

    // Find a stream with H.264 video data.
    for (uint32_t si = 0; si < in->context->nb_streams; ++si) {
        auto* maybe = in->context->streams[si];
        if (maybe->codecpar && maybe->codecpar->codec_id == AV_CODEC_ID_H264) {
            in->stream = maybe;
            break;
        }
    }

    if (!in->stream) {
        fmt::print("*** {}: No H.264 stream found\n", url);
        exit(1);
    }

    fmt::print(
        "H.264 stream (#{}): {}x{} {:.1f}fps\n",
        in->stream->index,
        in->stream->codecpar->width, in->stream->codecpar->height,
        av_q2d(in->stream->avg_frame_rate)
    );

    // Initialize the "mp4 to annex B" filter; libavcodec provides H.264
    // data in "mp4" format, V4L2 needs it with "Annex B" headers.
    // https://wiki.multimedia.cx/index.php/H.264
    auto const* const filter_type = av_bsf_get_by_name("h264_mp4toannexb");
    if (!filter_type) {
        fmt::print("*** No \"h264_mp4toannexb\" bitstream filter available\n");
        exit(1);
    }

    avcheck(
        av_bsf_alloc(filter_type, &in->filter),
        "Allocating MP4-to-AnnexB filter"
    );

    in->filter->par_in = in->stream->codecpar;
    in->filter->time_base_in = in->stream->time_base;
    avcheck(av_bsf_init(in->filter), "Initializing MP4-to-AnnexB filter");

    in->packet = av_packet_alloc();
    next_input_packet(*in);
    return in;
}

// Finds and opens a V4L2 device offering memory-to-memory H.264 decoding.
int open_decoder() {
    // Look for /dev/videoN entries.
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

        // Look for the capabilities and formats we need.
        // We don't use MPLANE support but it's what the Pi decoder has.
        // To be more general we'd check for non-MPLANE VIDEO_M2M also.
        v4l2_fmtdesc format = {};
        v4l2_capability cap = {};
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
            fmt::print("*** Querying ({})\n", native, strerror(errno));
        } else if (
            (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) &&
            (cap.capabilities & V4L2_CAP_STREAMING)
        ) {
            for (
                format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
                !ioctl(fd, VIDIOC_ENUM_FMT, &format);
                ++format.index
            ) {
               if (format.pixelformat == V4L2_PIX_FMT_H264) {
                   fmt::print("H.264 decoder: {}\n", native);
                   return fd;
               }
            }
        }

        close(fd);
    }

    fmt::print("*** No V4L2 H.264 decoder device found\n");
    exit(1);
}

// Formats a buffer type (to-decoder or from-encoder) for debugging.
std::string describe(v4l2_buf_type type) {
    switch (type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE: return "dec";
        case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE: return "enc";
        default: return fmt::format("?type={}?", type);
    }
}

// Formats a "field" type (interlacing status) for debugging.
std::string describe(v4l2_field const field) {
    switch (field) {
#define F(X) case V4L2_FIELD_##X: return #X
        F(ANY);
        F(NONE);
        F(TOP);
        F(BOTTOM);
        F(INTERLACED);
        F(SEQ_TB);
        F(SEQ_BT);
        F(ALTERNATE);
        F(INTERLACED_TB);
        F(INTERLACED_BT);
#undef F
        default: return fmt::format("?field={}?", field);
    }
}

// Formats a V4L2 buffer descriptor (not actual contents) for debugging.
std::string describe(v4l2_buffer const& buf) {
    std::string out = fmt::format(
        "{} buf #{:<2d} t={:03}.{:03} {:4}",
        describe((v4l2_buf_type) buf.type), buf.index,
        buf.timestamp.tv_sec, buf.timestamp.tv_usec / 1000,
        buf.m.planes[0].bytesused / 1024
    );
    if (buf.m.planes[0].length) {
        out += fmt::format("/{:4}kB", buf.m.planes[0].length / 1024);
    } else {
        out += fmt::format("kB     ");
    }
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
    return out + fmt::format(" {}", describe((v4l2_field) buf.field));
}

// Formats a V4L2 media format (and optional buffer count) for debugging.
std::string describe(v4l2_format const& format, int num_buffers) {
    std::string out = describe((v4l2_buf_type) format.type);
    auto const& pix = format.fmt.pix_mp;
    if (pix.width || pix.height) {
        out += fmt::format(" {}x{}", pix.width, pix.height);
    }
    out += fmt::format(
        " {:.4s} {}",
        (char const*) &pix.pixelformat,
        describe((v4l2_field) pix.field)
    );

    if (num_buffers) out += fmt::format(" {} x", num_buffers);
    out += " [";
    for (int pi = 0; pi < pix.num_planes; ++pi) {
        out += fmt::format(
            "{}{}kB", (pi ? " | " : ""), pix.plane_fmt[pi].sizeimage / 1024
        );
        if (pix.plane_fmt[pi].bytesperline) {
            out += fmt::format(" {}/l", pix.plane_fmt[pi].bytesperline);
        }
    }
    return out + "]";
}

// Queries V4L2 for buffers of the specified type, which must have been
// previously created on the device. Each buffer is memory-mapped and a
// MappedBuffer object generated for each one.
std::vector<std::unique_ptr<MappedBuffer>> mmap_decoder_buffers(
    int const fd, v4l2_buf_type const type
) {
    std::vector<std::unique_ptr<MappedBuffer>> maps;

    for (int bi = 0;; ++bi) {
        auto map = std::make_unique<MappedBuffer>();
        map->index = bi;

        v4l2_buffer query = {};
        query.type = type;
        query.index = bi;

        v4l2_plane query_plane = {};
        query.length = 1;
        query.m.planes = &query_plane;
        if (ioctl(fd, VIDIOC_QUERYBUF, &query)) {
            if (bi > 0 && errno == EINVAL) break;
            fmt::print("*** Query {}: {}\n", describe(query), strerror(errno));
            exit(1);
        }

        map->size = query.m.planes[0].length;
        map->mmap = mmap(
            nullptr, map->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
            query.m.planes[0].m.mem_offset
        );
        if (map->mmap == MAP_FAILED) {
           fmt::print("*** Memory mapping buffer: {}\n", strerror(errno));
           exit(1);
        }

        v4l2_exportbuffer expbuf = {};
        expbuf.type = type;
        expbuf.index = bi;
        expbuf.plane = 0;
        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf)) {
            fmt::print("*** Exporting buffer from V4L2: {}\n", strerror(errno));
            exit(1);
        }
        map->dmabuf_fd = expbuf.fd;
        maps.push_back(std::move(map));
    }
    return maps;
}

// Subscribes to V4L2 events of interest.
void setup_decoder_events(int const fd) {
    v4l2_event_subscription subscribe = {};
    subscribe.type = V4L2_EVENT_SOURCE_CHANGE;
    if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &subscribe)) {
        fmt::print("*** Subscribing to SOURCE_CHANGE: {}\n", strerror(errno));
        exit(1);
    }

    subscribe.type = V4L2_EVENT_EOS;
    if (ioctl(fd, VIDIOC_SUBSCRIBE_EVENT, &subscribe)) {
        fmt::print("*** Subscribing to EOS: {}\n", strerror(errno));
        exit(1);
    }
}

// Sets up the "OUTPUT" (app-to-device, i.e. decoder *input*) channel
// for H.264 input with a specified number of buffers.
void setup_encoded_stream(
    int const fd, // int const width, int const height,
    std::vector<std::unique_ptr<MappedBuffer>>* const buffers
) {
    if (!buffers->empty()) {
        fmt::print("*** Encoded stream buffers reconfigured?!?!\n");
        exit(1);
    }

    // Do not set width/height, the decoder will read size from the stream.
    v4l2_format encoded_format = {};
    encoded_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    encoded_format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    encoded_format.fmt.pix_mp.num_planes = 1;
    encoded_format.fmt.pix_mp.plane_fmt[0].sizeimage = encoded_buffer_size;
    if (ioctl(fd, VIDIOC_S_FMT, &encoded_format)) {
        fmt::print("*** Setting encoded format: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers encoded_reqbuf = {};
    encoded_reqbuf.type = encoded_format.type;
    encoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &encoded_reqbuf)) {
        fmt::print("*** Freeing encoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    encoded_reqbuf.count = encoded_buffer_count;
    if (ioctl(fd, VIDIOC_REQBUFS, &encoded_reqbuf)) {
        fmt::print("*** Creating encoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, VIDIOC_STREAMON, &encoded_format.type)) {
        fmt::print("*** Starting encoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = mmap_decoder_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    fmt::print("ON: {}\n", describe(encoded_format, encoded_reqbuf.count));
}

// Sets up the "CAPTURE" (device-to-app, i.e. decoder *output*) channel,
// querying the decoder for the picture format and size.
void setup_decoded_stream(
    int const fd,
    std::vector<std::unique_ptr<MappedBuffer>>* const buffers,
    v4l2_format* const format,
    v4l2_rect* const rect
) {
    buffers->clear();  // Unmap existing buffers, if any.

    *format = {};
    format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, format)) {
        fmt::print("*** Getting decoded format: {}\n", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, VIDIOC_STREAMOFF, &format->type)) {
        fmt::print("*** Stopping decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers decoded_reqbuf = {};
    decoded_reqbuf.type = format->type;
    decoded_reqbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Freeing decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    decoded_reqbuf.count = decoded_buffer_count;
    if (ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Creating decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, VIDIOC_STREAMON, &format->type)) {
        fmt::print("*** Starting decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = mmap_decoder_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    fmt::print("ON: {}\n", describe(*format, decoded_reqbuf.count));

    for (int const target : {0x0, 0x1, 0x2, 0x3, 0x100, 0x101, 0x102, 0x103}) {
        v4l2_selection selection = {};
        selection.type = format->type;
        selection.target = target;
        if (!ioctl(fd, VIDIOC_G_SELECTION, &selection)) {
            if (target == V4L2_SEL_TGT_COMPOSE) *rect = selection.r;
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

// Sets up a software JPEG encoder used to save frame files, if enabled.
void setup_jpeg(
    JpegState* const out,
    v4l2_rect const& rect
) {
    auto const* jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpeg_codec) {
        fmt::print("*** No MJPEG encoder found\n");
        exit(1);
    }

    avcodec_free_context(&out->context);
    out->context = avcodec_alloc_context3(jpeg_codec);
    if (!out->context) {
        fmt::print("*** Allocating JPEG encoder: {}\n", strerror(errno));
        exit(1);
    }

    // The JPEG encoder will take (a flavor of) YUV420, which is what the
    // H.264 decoder outputs, so we don't have to do color space conversion.
    out->context->pix_fmt = AV_PIX_FMT_YUVJ420P;  // "J" is JPEG color space
    out->context->width = rect.width;
    out->context->height = rect.height;
    out->context->time_base = {1, 30};  // Arbitrary but required.
    avcheck(
        avcodec_open2(out->context, jpeg_codec, nullptr),
        "Initializing JPEG encoder"
    );

    fmt::print(
        "JPEG encoder: {} ({}) {}x{}\n",
        avcodec_get_name(out->context->codec->id),
        av_get_pix_fmt_name(out->context->pix_fmt),
        out->context->width, out->context->height
    );

    if (!out->frame) {
        out->frame = av_frame_alloc();
        if (!out->frame) {
            fmt::print("*** Allocating frame: {}\n", strerror(errno));
            exit(1);
        }
    }

    if (!out->packet) {
        out->packet = av_packet_alloc();
        if (!out->packet) {
            fmt::print("*** Allocating packet: {}\n", strerror(errno));
            exit(1);
        }
    }
}

// Writes a decoded video frame as a JPEG file.
void save_jpeg(
    JpegState* const out,
    v4l2_format const& format,
    v4l2_rect const& rect,
    MappedBuffer const& map
) {
    if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        fmt::print("*** Bad decoded format type: {}\n", format.type);
        exit(1);
    }

    auto const& mp_format = format.fmt.pix_mp;
    if (mp_format.pixelformat != V4L2_PIX_FMT_YUV420) {
        fmt::print(
            "*** Unexpected decoded pixel format: {:.4s}\n",
            (char const*) &mp_format.pixelformat
        );
        exit(1);
    }

    if (!out->context) {
        setup_jpeg(out, rect);
    }

    auto* const f = out->frame;
    f->format = out->context->pix_fmt;
    f->width = rect.width;
    f->height = rect.height;

    // Y/U/V is concatenated by V4L2, but avcodec uses separate planes.
    uint8_t* const y = (uint8_t*) map.mmap;
    uint8_t* const u = y + mp_format.width * mp_format.height;
    uint8_t* const v = u + mp_format.width * mp_format.height / 4;
    f->linesize[0] = mp_format.width;
    f->linesize[1] = mp_format.width / 2;
    f->linesize[2] = mp_format.width / 2;
    f->data[0] = y + rect.left + rect.top * f->linesize[0];
    f->data[1] = u + rect.left / 2 + rect.top / 2 * f->linesize[1];
    f->data[2] = v + rect.left / 2 + rect.top / 2 * f->linesize[2];
    avcheck(
        avcodec_send_frame(out->context, out->frame),
        "Sending frame to JPEG encoder"
    );

    avcheck(
        avcodec_receive_packet(out->context, out->packet),
        "Receiving JPEG from encoder"
    );

    auto const path = fmt::format("{}.{:04}.jpeg", out->prefix, out->count++);
    if (print_io) {
        fmt::print("Saving JPEG ({}b): {}\n", out->packet->size, path);
    }

    int const fd = open(path.c_str(), O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        fmt::print("*** {}: {}\n", path, strerror(errno));
        exit(1);
    }

    if (write(fd, out->packet->data, out->packet->size) < 0) {
        fmt::print("*** {}: {}\n", path, strerror(errno));
        exit(1);
    }

    close(fd);
}

// Initializes KMS for displaying video frames on screen.
void setup_screen(ScreenState* const out) {
    out->fd = open(out->device.c_str(), O_RDWR);
    if (out->fd < 0) {
        fmt::print("*** {}: {}\n", out->device, strerror(errno));
        exit(1);
    }

    if (drmSetMaster(out->fd)) {
        fmt::print("*** Claiming ({}): {}\n", out->device, strerror(errno));
        exit(1);
    }

    drmSetClientCap(out->fd, DRM_CLIENT_CAP_ATOMIC, 1);
    drmSetClientCap(out->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    auto* const res = drmModeGetResources(out->fd);
    if (!res) {
        fmt::print("*** Resources ({}): {}\n", out->device, strerror(errno));
        exit(1);
    }

    // Use the first active connector (video output) if it wasn't specified.
    for (int ci = 0; ci < res->count_connectors && !out->connector_id; ++ci) {
        auto* const conn = drmModeGetConnector(out->fd, res->connectors[ci]);
        if (conn) {
            if (conn->connection == DRM_MODE_CONNECTED) {
                out->connector_id = conn->connector_id;
            }
            drmModeFreeConnector(conn);
        }
    }

    if (!out->connector_id) {
        fmt::print("*** {}: No active connector found\n");
        exit(1);
    }

    auto* conn = drmModeGetConnector(out->fd, out->connector_id);
    if (!conn) {
        fmt::print("*** Screen #{}: {}\n", out->connector_id, strerror(errno));
        exit(1);
    }

    // Get the preferred video mode used by the selected container.
    out->mode_blob = 0;
    for (int mi = 0; mi < conn->count_modes && !out->mode_blob; ++mi) {
        if (conn->modes[mi].type & DRM_MODE_TYPE_PREFERRED) {
            out->mode_info = conn->modes[mi];
            if (drmModeCreatePropertyBlob(
                out->fd, &out->mode_info, sizeof(out->mode_info),
                &out->mode_blob
            )) {
                fmt::print("*** Creating blob: {}\n", strerror(errno));
                exit(1);
            }
        }
    }
    if (!out->mode_blob) {
        fmt::print("*** No PREFERRED mode for conn #{}\n", out->connector_id);
        exit(1);
    }

    // Use the first encoder associated with the connector to find
    // a CRTC compatible with the connector.
    auto* const enc = drmModeGetEncoder(out->fd, conn->encoders[0]);
    if (!enc) {
        fmt::print("*** Encoder #{}: {}\n", conn->encoders[0], strerror(errno));
        exit(1);
    }
    if (!enc->possible_crtcs) {
        fmt::print("*** Encoder #{}: No CRTCs\n", conn->encoders[0]);
        exit(1);
    }

    int crtc_index = 0;
    while (!(enc->possible_crtcs & (1u << crtc_index))) ++crtc_index;
    out->crtc_id = res->crtcs[crtc_index];

    auto* const planes = drmModeGetPlaneResources(out->fd);
    if (!planes) {
        fmt::print("*** Planes ({}): {}\n", out->device, strerror(errno));
        exit(1);
    }

    // Find a video plane compatible with the selected CRTC.
    // This assumes the first plane will be the "primary" plane.
    out->plane_id = 0;
    for (uint32_t pi = 0; !out->plane_id && pi < planes->count_planes; ++pi) {
        auto* plane = drmModeGetPlane(out->fd, planes->planes[pi]);
        if (!plane) {
            fmt::print(
               "*** Plane #{}: {}\n", planes->planes[pi], strerror(errno)
            );
            exit(1);
        }
        if (plane->possible_crtcs & (1 << crtc_index)) {
            out->plane_id = plane->plane_id;
        }
        drmModeFreePlane(plane);
    }
    if (!out->plane_id) {
        fmt::print("*** CRTC #{}: No compatible plane found\n", out->crtc_id);
        exit(1);
    }

    // Scan property definitions on the connector, CRTC, and plane objects,
    // for use in atomic mode setting updates.
    for (auto id : {out->connector_id, out->crtc_id, out->plane_id}) {
        auto const any_type = DRM_MODE_OBJECT_ANY;
        auto* const props = drmModeObjectGetProperties(out->fd, id, any_type);
        for (uint32_t pi = 0; props && pi < props->count_props; ++pi) {
            if (!out->prop_id.count(props->props[pi])) {
                auto* const p = drmModeGetProperty(out->fd, props->props[pi]);
                if (p && (p->flags & DRM_MODE_PROP_ATOMIC)) {
                    out->prop_id[p->prop_id] = p;
                    out->prop_name[p->name] = p;
                }
            }
        }
    }

    // Allocate an atomic mode setting request for later use.
    out->atomic_req = drmModeAtomicAlloc();
    if (!out->atomic_req) {
        fmt::print("*** Allocating atomic request: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print(
        "SCREEN: {} plane={} » crtc={} » enc={} » conn={} ({}x{}x{}Hz)\n",
        out->device, out->plane_id, out->crtc_id, enc->encoder_id,
        out->connector_id, out->mode_info.hdisplay, out->mode_info.vdisplay,
        out->mode_info.vrefresh
    );

    drmModeFreePlaneResources(planes);
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
}

// Initializes a video buffer (from V4L2) to use as a KMS framebuffer.
void setup_screen_buffer(
    ScreenState* const out,
    v4l2_format const& format,
    v4l2_rect const& rect,
    MappedBuffer* map
) {
    if (drmPrimeFDToHandle(out->fd, map->dmabuf_fd, &map->drm_handle)) {
        fmt::print("*** Importing buffer to DRM: {}\n", strerror(errno));
    }

    auto const& mp_format = format.fmt.pix_mp;
    if (mp_format.pixelformat != V4L2_PIX_FMT_YUV420) {
        fmt::print(
            "*** Unexpected decoded pixel format: {:.4s}\n",
            (char const*) &mp_format.pixelformat
        );
        exit(1);
    }

    // Y/U/V is concatenated by V4L2, but DRM uses separate planes.
    uint32_t const y = 0;
    uint32_t const u = y + mp_format.width * mp_format.height;
    uint32_t const v = u + mp_format.width * mp_format.height / 4;

    uint32_t const drm_handles[4] = {
        map->drm_handle,
        map->drm_handle,
        map->drm_handle,
        0  // Unused
    };
    uint32_t const pitches[4] = {
        mp_format.width,
        mp_format.width / 2,
        mp_format.width / 2,
        0  // Unused
    };
    uint32_t const offsets[4] = {
        y + rect.left + rect.top * pitches[0],
        u + rect.left / 2 + rect.top / 2 * pitches[1],
        v + rect.left / 2 + rect.top / 2 * pitches[2],
        0  // Unused
    };

    // Create a framebuffer object using the memory buffer.
    if (drmModeAddFB2(
        out->fd, rect.width, rect.height, DRM_FORMAT_YUV420,
        drm_handles, pitches, offsets, &map->drm_framebuffer, 0
    )) {
        fmt::print("*** Creating DRM framebuffer: {}\n", strerror(errno));
        exit(1);
    }
}

// Shows a decoded video frame on screen. Returns once the frame is visible.
// Since the buffer is being directly used as a framebuffer, it should not
// be recycled until some other buffer is shown.
void show_screen(
    ScreenState* const out,
    v4l2_format const& format,
    v4l2_rect const& rect,
    MappedBuffer* const map
) {
    if (out->fd < 0) setup_screen(out);
    if (!map->drm_framebuffer) setup_screen_buffer(out, format, rect, map);

    auto const add = [=](uint32_t id, char const* name, uint64_t v) {
        auto const prop = out->prop_name[name];
        if (!prop) {
            fmt::print("*** Unknown property: \"{}\"\n", name);
            exit(1);
        }
        if (!drmModeAtomicAddProperty(out->atomic_req, id, prop->prop_id, v)) {
            fmt::print("*** Adding property: {}\n", strerror(errno));
            exit(1);
        }
    };

    // Use atomic modesetting to make this buffer visible.
    drmModeAtomicSetCursor(out->atomic_req, 0);    
    add(out->connector_id, "CRTC_ID", out->crtc_id);
    add(out->crtc_id, "ACTIVE", 1);
    add(out->crtc_id, "MODE_ID", out->mode_blob);
    add(out->plane_id, "FB_ID", map->drm_framebuffer);
    add(out->plane_id, "CRTC_ID", out->crtc_id);
    add(out->plane_id, "CRTC_X", 0);
    add(out->plane_id, "CRTC_Y", 0);
    add(out->plane_id, "CRTC_W", out->mode_info.hdisplay);
    add(out->plane_id, "CRTC_H", out->mode_info.vdisplay);
    add(out->plane_id, "SRC_X", 0);
    add(out->plane_id, "SRC_Y", 0);
    add(out->plane_id, "SRC_W", rect.width << 16);
    add(out->plane_id, "SRC_H", rect.height << 16);

    if (print_io) {
        fmt::print(
            "Showing   dec buf #{:<2d} fb={:<3d} » plane={} » "
            "crtc={} » conn={} ({}x{}x{}Hz)\n",
            map->index, map->drm_framebuffer,
            out->plane_id, out->crtc_id, out->connector_id,
            out->mode_info.hdisplay, out->mode_info.vdisplay,
            out->mode_info.vrefresh
        );
    }

    if (drmModeAtomicCommit(
        out->fd, out->atomic_req, DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr
    )) {
        fmt::print("*** DRM atomic commit: {}\n", strerror(errno));
        exit(1);
    }
}

// Runs the main decoding loop.
void run_decoder(
    InputFromFile const& input,
    int const fd,
    JpegState* const jpeg_state,
    ScreenState* const screen_state
) {
    if (!input.packet->buf) {
        fmt::print("*** No packets in input stream #{}\n", input.stream->index);
        exit(1);
    }

    std::vector<std::unique_ptr<MappedBuffer>> encoded_maps;
    std::vector<std::unique_ptr<MappedBuffer>> decoded_maps;

    setup_decoder_events(fd);
    setup_encoded_stream(fd, &encoded_maps);
    // Don't set up decoded stream until SOURCE_CHANGE event.

    MappedBuffer* on_screen = nullptr;
    std::deque<MappedBuffer*> encoded_free, decoded_free;
    for (auto const &m : encoded_maps) encoded_free.push_back(m.get());

    auto const encoded_buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    auto const decoded_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    auto const time_base = av_q2d(input.stream->time_base);
    v4l2_format decoded_format = {};
    v4l2_rect valid_rect = {};

    int decoded_count = 0;
    double last_received_s = 0;
    auto const start_t = std::chrono::steady_clock::now();

    bool end_of_stream = false;
    bool decoded_changed = false;
    bool decoded_done = true;  // Until SOURCE_CHANGE.
    while (!end_of_stream || !decoded_done || input.packet->buf) {
        bool made_progress = false;  // Used to control busy-loop delay.

        // Reclaim encoded buffers once consumed by the decoder.
        v4l2_buffer dqbuf = {};
        dqbuf.type = encoded_buf_type;
        dqbuf.memory = V4L2_MEMORY_MMAP;

        v4l2_plane dqbuf_plane = {};
        dqbuf.length = 1;
        dqbuf.m.planes = &dqbuf_plane;

        while (!ioctl(fd, VIDIOC_DQBUF, &dqbuf)) {
            if (dqbuf.index > encoded_maps.size()) {
                fmt::print("*** Bad reclaimed index: #{}\n", dqbuf.index);
                exit(1);
            }
            if (print_io) {
                fmt::print("Reclaimed {}\n", describe(dqbuf));
            }
            encoded_free.push_back(encoded_maps[dqbuf.index].get());
            made_progress = true;
        }
        if (errno != EAGAIN) {
            fmt::print("*** Reclaiming encoded buffer: {}\n", strerror(errno));
            exit(1);
        }

        // Push encoded data into the decoder.
        while (!encoded_free.empty() && input.packet->buf) {
            auto* map = encoded_free.front();
            encoded_free.pop_front();
            if (input.packet->size > (int) map->size) {
                fmt::print(
                    "*** Coded packet ({}kB) too big for buffer ({}kB)\n",
                    input.packet->size / 1024, map->size / 1024
                );
                exit(1);
            }

            v4l2_buffer qbuf = {};
            qbuf.type = encoded_buf_type;
            qbuf.index = map->index;
            qbuf.memory = V4L2_MEMORY_MMAP;

            v4l2_plane qbuf_plane = {};
            qbuf.length = 1;
            qbuf.m.planes = &qbuf_plane;
            qbuf.m.planes[0].bytesused = input.packet->size;

            if (input.packet->pts != AV_NOPTS_VALUE) {
                double const time = input.packet->pts * time_base;
                qbuf.timestamp.tv_sec = time;
                qbuf.timestamp.tv_usec = (time - qbuf.timestamp.tv_sec) * 1e6;
            }

            memcpy(map->mmap, input.packet->data, input.packet->size);
            if (print_io) {
                fmt::print("Sending   {}\n", describe(qbuf));
            }
            if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
                fmt::print("*** Sending encoded buffer: {}\n", strerror(errno));
                exit(1);
            }

            made_progress = true;
            next_input_packet(input);
            if (!input.packet->buf) {
                v4l2_decoder_cmd command = {};
                command.cmd = V4L2_DEC_CMD_STOP;
                if (ioctl(fd, VIDIOC_DECODER_CMD, &command)) {
                    fmt::print("*** Sending STOP: {}\n", strerror(errno));
                    exit(1);
                }
                fmt::print("--- Encoded data fully sent => sent STOP\n");
            }
        }

        // Send free decoded buffers to be filled by the decoder.
        while (!decoded_changed && !decoded_done && !decoded_free.empty()) {
            auto* map = decoded_free.front();
            decoded_free.pop_front();

            v4l2_buffer qbuf = {};
            qbuf.type = decoded_buf_type;
            qbuf.index = map->index;
            qbuf.memory = V4L2_MEMORY_MMAP;

            v4l2_plane qbuf_plane = {};
            qbuf.length = 1;
            qbuf.m.planes = &qbuf_plane;

            if (print_io) {
                fmt::print("Recycling {}\n", describe(qbuf));
            }
            if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
                fmt::print("*** Recycling buffer: {}\n", strerror(errno));
                exit(1);
            }
            made_progress = true;
        }

        // Receive decoded data and add the buffers to the free list.
        // Only process *one* frame to prioritize keeping the decoder busy.
        if (!decoded_done) {
            v4l2_buffer dqbuf = {};
            dqbuf.type = decoded_buf_type;
            dqbuf.memory = V4L2_MEMORY_MMAP;

            v4l2_plane dqbuf_plane = {};
            dqbuf.length = 1;
            dqbuf.m.planes = &dqbuf_plane;

            if (ioctl(fd, VIDIOC_DQBUF, &dqbuf)) {
                if (errno == EPIPE) {
                    fmt::print("--- EPIPE => Decoded stream fully drained\n");
                    decoded_done = true;
                    continue;
                } else if (errno != EAGAIN) {
                    fmt::print("*** Receiving buffer: {}\n", strerror(errno));
                    exit(1);
                }
            } else {
                auto const received_t = std::chrono::steady_clock::now();
                if (print_io) {
                    fmt::print("Received  {}\n", describe(dqbuf));
                }
                if (dqbuf.index > decoded_maps.size()) {
                    fmt::print("*** Bad decoded index: #{}\n", dqbuf.index);
                    exit(1);
                }

                auto* map = decoded_maps[dqbuf.index].get();
                if (dqbuf.flags & V4L2_BUF_FLAG_LAST) {
                    fmt::print("--- LAST flag => Decoded stream is drained\n");
                    decoded_done = true;
                }

                auto const received_s =
                    std::chrono::duration<double>(received_t - start_t).count();
                auto const scheduled_s =
                    dqbuf.timestamp.tv_sec + 1e-6 * dqbuf.timestamp.tv_usec +
                    start_delay;

                if (!flat_out && scheduled_s > received_s) {
                    double const sleep_s = scheduled_s - received_s;
                    if (print_io) fmt::print("Sleep for {:.3f}s\n", sleep_s);
                    std::chrono::duration<double> sleep_d{sleep_s};
                    std::this_thread::sleep_for(sleep_d);
                }

                if (!jpeg_state->prefix.empty()) {
                    save_jpeg(jpeg_state, decoded_format, valid_rect, *map);
                }

                if (!screen_state->device.empty()) {
                    // Don't recycle the buffer until it's offscreen!
                    show_screen(screen_state, decoded_format, valid_rect, map);
                    if (on_screen) decoded_free.push_back(on_screen);
                    on_screen = map;
                } else {
                    decoded_free.push_back(map);
                }

                fmt::print(
                    "Frame #{:<4} {:6.3f}s {:+.3f} {:.3f}/f; "
                    "sched {:6.3f}s {:.3f}/f ({:+.3f}s {})\n",
                    decoded_count,
                    received_s, received_s - last_received_s,
                    decoded_count ? received_s / decoded_count : 0.0,
                    scheduled_s, 
                    decoded_count ? scheduled_s / decoded_count : 0.0,
                    received_s - scheduled_s,
                    scheduled_s > received_s ? "early" : "late"
                );

                last_received_s = received_s;
                made_progress = true;
                ++decoded_count;
            }
        }

        // Receive events. This must happen AFTER decoded-stream DQBUF, so we
        // don't get spurious end-of-stream markers due to source changes.
        v4l2_event event = {};
        while (!ioctl(fd, VIDIOC_DQEVENT, &event)) {
            if (event.type == V4L2_EVENT_SOURCE_CHANGE) {
                fmt::print("--- SOURCE_CHANGE event => will reconfigure\n");
                decoded_changed = true;
            } else if (event.type == V4L2_EVENT_EOS) {
                fmt::print("--- EOS event => encoded data fully processed\n");
                end_of_stream = true;
            } else {
                fmt::print("*** Unknown event ?type={}?\n", event.type);
            }
        }
        if (errno != ENOENT) {
            fmt::print("*** Receiving event: {}\n", strerror(errno));
            exit(1);
        }

        if (decoded_done && decoded_changed) {
            fmt::print(
                 "--- SOURCE_CHANGE received & decoded stream drained "
                 "=> reconfiguring\n"
            );
            decoded_done = decoded_changed = false;  // Restart
            decoded_free.clear();
            setup_decoded_stream(
                fd, &decoded_maps, &decoded_format, &valid_rect
            );
            for (auto const &m : decoded_maps) decoded_free.push_back(m.get());
        }

        if (!made_progress) {
            // Wait 10ms before polling again. (Could use poll() instead.)
            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// Main program, parses flags and calls the decoder loop.
int main(int const argc, char const* const* const argv) {
    std::string media_file;
    JpegState jpeg_state = {};
    ScreenState screen_state = {};

    CLI::App app("Use V4L2 to decode an H.264 video file");
    app.add_option("--media", media_file, "Media file or URL")->required();
    app.add_option(
        "--frame_prefix", jpeg_state.prefix, "Save JPEGs with this prefix"
    );
    app.add_option(
        "--screen_dev", screen_state.device, "Display on this DRI device"
    );
    app.add_option(
        "--screen_id", screen_state.connector_id, "Use this DRI connector ID"
    )->needs("--screen_dev");

    app.add_flag("--flat_out", flat_out, "Decode without frame rate delay");
    app.add_option("--start_delay", start_delay, "Startup prebuffering time");
    app.add_option("--encoded_buffer_size", encoded_buffer_size, "Bytes/buf");
    app.add_option("--encoded_buffers", encoded_buffer_count, "Buffer count");
    app.add_option("--decoded_buffers", decoded_buffer_count, "Buffer count");
    app.add_flag("--print_io", print_io, "Print buffer operations");

    CLI11_PARSE(app, argc, argv);

    if (screen_state.device == "none" || screen_state.device == "/dev/null")
        screen_state.device.clear();

    auto const input = open_input(media_file);
    int const decoder_fd = open_decoder();

    run_decoder(*input, decoder_fd, &jpeg_state, &screen_state);
    close(decoder_fd);
    fmt::print("Closed and complete!\n");
    return 0;
}
