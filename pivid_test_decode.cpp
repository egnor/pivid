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

// Set by flags in main()
bool print_io_flag = false;

struct InputFromFile {
    AVFormatContext* context = nullptr;
    AVStream* stream = nullptr;
    AVBSFContext* filter = nullptr;
    AVPacket* packet = nullptr;

    InputFromFile() {}
    ~InputFromFile() {
        if (packet) av_packet_free(&packet);
        // if (filter) av_bsf_free(&filter);  // Seems to cause double-free??
        if (context) avformat_close_input(&context);
    }

    InputFromFile(InputFromFile const&) = delete;
    void operator=(InputFromFile const&) = delete;
};

struct JpegOutput {
    std::string prefix;
    AVCodecContext* context = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int count = 0;

    JpegOutput() {}
    ~JpegOutput() {
        if (packet) av_packet_unref(packet);
        if (frame) av_frame_free(&frame);
        if (context) avcodec_free_context(&context);
    }

    JpegOutput(JpegOutput const&) = delete;
    void operator=(JpegOutput const&) = delete;
};

struct ScreenOutput {
    std::string device;
    uint32_t connector_id = 0;
    uint32_t crtc_id = 0;
    int fd = -1;

    ScreenOutput() {}
    ~ScreenOutput() {
        if (fd >= 0) close(fd);
    }

    ScreenOutput(ScreenOutput const&) = delete;
    void operator=(ScreenOutput const&) = delete;
};

struct MappedBuffer {
    int index = 0;
    void *mmap = MAP_FAILED;
    size_t size = 0;
    int dmabuf_fd = -1;
    uint32_t drm_handle = 0;
    uint32_t drm_framebuffer = 0;

    MappedBuffer() {}
    ~MappedBuffer() {
        if (mmap != MAP_FAILED) munmap(mmap, size);
        if (dmabuf_fd >= 0) close(dmabuf_fd);
    }

    MappedBuffer(MappedBuffer const&) = delete;
    void operator=(MappedBuffer const&) = delete;
};

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
void input_next_packet(InputFromFile const& input) {
    for (;;) {
        av_packet_unref(input.packet);
        int const filt_err = av_bsf_receive_packet(input.filter, input.packet);
        if (filt_err >= 0 || filt_err == AVERROR_EOF) return;
        if (filt_err != AVERROR(EAGAIN)) avcheck(filt_err, "Input filter");

        do {
            av_packet_unref(input.packet);
            int const codec_err = av_read_frame(input.context, input.packet);
            if (codec_err == AVERROR_EOF) return;
            if (codec_err < 0) avcheck(codec_err, "Reading input");
        } while (input.packet->stream_index != input.stream->index);

        av_bsf_send_packet(input.filter, input.packet);
    }
}

// Opens media file with libavformat and finds H.264 stream
std::unique_ptr<InputFromFile> open_input(const std::string& url) {
    auto in = std::make_unique<InputFromFile>();
    avcheck(
        avformat_open_input(&in->context, url.c_str(), nullptr, nullptr),
        url
    );

    avcheck(avformat_find_stream_info(in->context, nullptr), "Stream info");

    fmt::print("Media file: {}\n", url);

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
    input_next_packet(*in);
    return in;
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

std::string describe(v4l2_buf_type type) {
    switch (type) {
        case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE: return "dec";
        case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE: return "enc";
        default: return fmt::format("?type={}?", type);
    }
}

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
    return out + fmt::format(" {}", describe((v4l2_field) buf.field));
}

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

std::vector<std::unique_ptr<MappedBuffer>> decoder_mmap_buffers(
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

void decoder_setup_events(int const fd) {
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

// "OUTPUT" (app-to-device, i.e. decoder *input*) setup
void decoder_setup_encoded_stream(
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

    encoded_reqbuf.count = 16;
    if (ioctl(fd, VIDIOC_REQBUFS, &encoded_reqbuf)) {
        fmt::print("*** Creating encoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, VIDIOC_STREAMON, &encoded_format.type)) {
        fmt::print("*** Starting encoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = decoder_mmap_buffers(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    fmt::print("ON: {}\n", describe(encoded_format, encoded_reqbuf.count));
}

// "CAPTURE" (device-to-app, i.e. decoder *output*) setup
void decoder_setup_decoded_stream(
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

    decoded_reqbuf.count = 16;
    if (ioctl(fd, VIDIOC_REQBUFS, &decoded_reqbuf)) {
        fmt::print("*** Creating decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    if (ioctl(fd, VIDIOC_STREAMON, &format->type)) {
        fmt::print("*** Starting decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    *buffers = decoder_mmap_buffers(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
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

// Write a decoded video frame to disk
void save_jpeg(
    JpegOutput* const out,
    v4l2_format const& format,
    v4l2_rect const& rect,
    MappedBuffer const& map
) {
    if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        fmt::print("*** Bad decoded format type: {}\n", format.type);
        exit(1);
    }

    auto const jpeg_pix_fmt = AV_PIX_FMT_YUVJ420P;  // JPEG color space
    auto const& mp_format = format.fmt.pix_mp;
    if (mp_format.pixelformat != V4L2_PIX_FMT_YUV420) {
        fmt::print(
            "*** Unexpected decoded pixel format: {:.4s}\n",
            (char const*) &mp_format.pixelformat
        );
        exit(1);
    }

    if (
        !out->context ||
        out->context->pix_fmt != jpeg_pix_fmt ||
        out->context->width != (int) rect.width ||
        out->context->height != (int) rect.height
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

        out->context->pix_fmt = jpeg_pix_fmt;
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
    }

    if (!out->frame) {
        out->frame = av_frame_alloc();
        if (!out->frame) {
            fmt::print("*** Allocating frame: {}\n", strerror(errno));
            exit(1);
        }
    }

    auto* const f = out->frame;
    f->format = jpeg_pix_fmt;
    f->width = rect.width;
    f->height = rect.height;

    // Y/U/V is concatenated in V4L2, but avcodec uses separate planes.
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

    if (!out->packet) {
        out->packet = av_packet_alloc();
        if (!out->packet) {
            fmt::print("*** Allocating packet: {}\n", strerror(errno));
            exit(1);
        }
    }

    avcheck(
        avcodec_receive_packet(out->context, out->packet),
        "Receiving JPEG from encoder"
    );

    auto const path = fmt::format("{}.{:04d}.jpeg", out->prefix, out->count++);
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

void setup_screen(
    ScreenOutput* const out,
    v4l2_format const& format,
    v4l2_rect const& rect
)
{
    out->fd = open(out->device.c_str(), O_RDWR);
    if (out->fd < 0) {
        fmt::print("*** {}: {}\n", out->device, strerror(errno));
        exit(1);
    }

    if (drmSetMaster(out->fd)) {
        fmt::print("*** Claiming ({}): {}\n", out->device, strerror(errno));
    }

    drmSetClientCap(out->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    auto* const res = drmModeGetResources(out->fd);
    if (!res) {
        fmt::print("*** Reading ({}): {}\n", out->device, strerror(errno));
        exit(1);
    }

    auto* const conn = drmModeGetConnector(out->fd, out->connector_id);
    if (!conn) {
        fmt::print("*** Screen #{}: {}\n", out->connector_id, strerror(errno));
        exit(1);
    }

    auto* const enc = drmModeGetEncoder(out->fd, conn->encoders[0]);
    if (!enc) {
        fmt::print("*** Encoder #{}: {}\n", conn->encoders[0], strerror(errno));
        exit(1);
    }
    if (!enc->possible_crtcs) {
        fmt::print("*** Encoder #{}: No CRTCs\n");
        exit(1);
    }

    int crtc_index = 0;
    while (!(enc->possible_crtcs & (1u << crtc_index))) ++crtc_index;
    out->crtc_id = res->crtcs[crtc_index];
    auto* const crtc = drmModeGetCrtc(out->fd, out->crtc_id);
    if (!crtc) {
        fmt::print("*** CRTC #{}: {}\n", out->crtc_id, strerror(errno));
        exit(1);
    }

    // TODO: set mode?
    (void) format;
    (void) rect;

    drmModeFreeCrtc(crtc);
    drmModeFreeEncoder(enc);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
}

void setup_screen_buffer(
    ScreenOutput* const out,
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

    // Y/U/V is concatenated in V4L2, but DRM uses separate planes.
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

    if (drmModeAddFB2(
        out->fd, rect.width, rect.height, DRM_FORMAT_YUV420,
        drm_handles, pitches, offsets, &map->drm_framebuffer, 0
    )) {
        fmt::print("*** Creating DRM framebuffer: {}\n", strerror(errno));
        exit(1);
    }
}

// Show a decoded video frame on screen
void show_screen(
    ScreenOutput* const out,
    v4l2_format const& format,
    v4l2_rect const& rect,
    MappedBuffer* map
) {
    if (out->fd < 0) setup_screen(out, format, rect);
    if (!map->drm_framebuffer) setup_screen_buffer(out, format, rect, map);

    // TODO don't recycle buffer until it's swapped out!

    fmt::print("crtc {} => fb {} (conn {})\n", out->crtc_id, map->drm_framebuffer, out->connector_id);
    if (drmModeSetCrtc(
        out->fd, out->crtc_id, map->drm_framebuffer,
        0, 0, &out->connector_id, 1, nullptr
    )) {
        fmt::print("*** Showing framebuffer: {}\n", strerror(errno));
        exit(1);
    }
}

void decoder_run(
    InputFromFile const& input,
    int const fd,
    JpegOutput* jpeg_output,
    ScreenOutput* screen_output
) {
    if (!input.packet->buf) {
        fmt::print("*** No packets in input stream #{}\n", input.stream->index);
        exit(1);
    }

    std::vector<std::unique_ptr<MappedBuffer>> encoded_maps;
    std::vector<std::unique_ptr<MappedBuffer>> decoded_maps;

    decoder_setup_events(fd);
    decoder_setup_encoded_stream(fd, &encoded_maps);
    // Don't set up decoded stream until SOURCE_CHANGE event.

    std::deque<MappedBuffer*> encoded_free, decoded_free;
    for (auto const &m : encoded_maps) encoded_free.push_back(m.get());

    auto const encoded_buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    auto const decoded_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    auto const time_base = av_q2d(input.stream->time_base);
    v4l2_format decoded_format = {};
    v4l2_rect decoded_rect = {};

    int frame_count = 0;
    time_t frame_sec = 0;
    auto const start_t = std::chrono::steady_clock::now();

    bool end_of_stream = false;
    bool decoded_changed = false;
    bool decoded_done = true;  // Until SOURCE_CHANGE.
    while (!end_of_stream || !decoded_done || input.packet->buf) {
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
            if (print_io_flag) {
                fmt::print("Reclaimed {}\n", describe(dqbuf));
            }
            encoded_free.push_back(encoded_maps[dqbuf.index].get());
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
            if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
                fmt::print("*** Sending encoded buffer: {}\n", strerror(errno));
                exit(1);
            }
            if (print_io_flag) {
                fmt::print("Sent      {}\n", describe(qbuf));
            }

            input_next_packet(input);
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

            if (ioctl(fd, VIDIOC_QBUF, &qbuf)) {
                fmt::print("*** Recycling buffer: {}\n", strerror(errno));
                exit(1);
            } else if (print_io_flag) {
                fmt::print("Recycled  {}\n", describe(qbuf));
            }
        }

        // Receive decoded data and add the buffers to the free list.
        while (!decoded_done) {
            v4l2_buffer dqbuf = {};
            dqbuf.type = decoded_buf_type;
            dqbuf.memory = V4L2_MEMORY_MMAP;

            v4l2_plane dqbuf_plane = {};
            dqbuf.length = 1;
            dqbuf.m.planes = &dqbuf_plane;

            if (ioctl(fd, VIDIOC_DQBUF, &dqbuf)) {
                if (errno == EAGAIN) break;  // No more buffers.
                if (errno == EPIPE) {
                    fmt::print("--- EPIPE => Decoded stream fully drained\n");
                    decoded_done = true;
                    continue;
                }
                fmt::print("*** Receiving buffer: {}\n", strerror(errno));
                exit(1);
            }

            if (dqbuf.index > decoded_maps.size()) {
                fmt::print("*** Bad decoded index: #{}\n", dqbuf.index);
                exit(1);
            }

            auto* map = decoded_maps[dqbuf.index].get();
            if (print_io_flag) {
                fmt::print("Received  {}\n", describe(dqbuf));
            }
            if (dqbuf.flags & V4L2_BUF_FLAG_LAST) {
                fmt::print("--- LAST flag => Decoded stream is drained\n");
                decoded_done = true;
            }

            if (!jpeg_output->prefix.empty()) {
                save_jpeg(jpeg_output, decoded_format, decoded_rect, *map);
            }

            if (!screen_output->device.empty()) {
                show_screen(screen_output, decoded_format, decoded_rect, map);
            }

            decoded_free.push_back(map);

            ++frame_count;
            if (dqbuf.timestamp.tv_sec > frame_sec) {
                frame_sec = dqbuf.timestamp.tv_sec;
                double const elapsed_t = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_t).count();
                fmt::print(
                    "::: {}fr decoded / {:.1f}s elapsed = {:.2f} fps\n",
                    frame_count, elapsed_t, frame_count / elapsed_t);
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
            decoder_setup_decoded_stream(
                fd, &decoded_maps, &decoded_format, &decoded_rect
            );
            for (auto const &m : decoded_maps) decoded_free.push_back(m.get());
        }

        // Wait 10ms before polling again -- TODO use poll() instead?
        fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main(int argc, char** argv) {
    std::string input_file;
    JpegOutput jpeg_output = {};
    ScreenOutput screen_output = {};

    CLI::App app("Use V4L2 to decode an H.264 video file");
    app.add_option("--input", input_file, "Media file or URL")->required();
    app.add_flag("--print_io", print_io_flag, "Print buffer operations");
    app.add_option(
        "--frame_prefix", jpeg_output.prefix, "Save JPEGs with this prefix"
    );
    app.add_option(
        "--screen_device", screen_output.device, "Show on this DRI device"
    )->excludes("--frame_prefix");
    app.add_option(
        "--screen_id", screen_output.connector_id, "DRI connector ID"
    )->needs("--screen_device");

    CLI11_PARSE(app, argc, argv);

    if (!screen_output.device.empty() && !screen_output.connector_id) {
       fmt::print("*** --screen_device requires --screen_id\n");
       exit(1);
    }

    auto const input = open_input(input_file);
    int const decoder_fd = open_decoder();

    decoder_run(*input, decoder_fd, &jpeg_output, &screen_output);
    close(decoder_fd);
    fmt::print("Closed and complete!\n");
    return 0;
}
