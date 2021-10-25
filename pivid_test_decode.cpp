// Simple command line tool to exercise h.264 decoding via V4L2

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cctype>
#include <filesystem>

#include <fmt/core.h>
#include <gflags/gflags.h>
#include <libv4l2.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
}

DEFINE_int32(coded_buffers, 16, "Buffered frames for decoder input");
DEFINE_int32(decoded_buffers, 2, "Buffered frames for decoder output");

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
void open_v4l2_h264_decoder(int* fd) {
    std::filesystem::path const dev_dir = "/dev";
    for (const auto& entry : std::filesystem::directory_iterator(dev_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 5) != "video" || !isdigit(filename[5])) {
            continue;
        }

        auto const& native = entry.path().native();
        *fd = open(native.c_str(), O_RDWR);
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

void setup_decoder(int const fd, int width, int height) {
    // https://www.kernel.org/doc/html/v5.10/userspace-api/media/v4l/dev-decoder.html

    //
    // Stop any existing streaming and free any buffers in use
    //

    v4l2_requestbuffers coded_buffers = {};
    coded_buffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    coded_buffers.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &coded_buffers)) {
        fmt::print("*** Releasing H.264 buffers: {}\n", strerror(errno));
        exit(1);
    }

    v4l2_requestbuffers decoded_buffers = {};
    decoded_buffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    decoded_buffers.memory = V4L2_MEMORY_MMAP;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_buffers)) {
        fmt::print("*** Releasing decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    //
    // "OUTPUT" (app-to-device, i.e. decoder *input*) setup
    //

    v4l2_format coded_format = {};
    coded_format.type = coded_buffers.type;
    coded_format.fmt.pix_mp.width = width;
    coded_format.fmt.pix_mp.height = height;
    coded_format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    coded_format.fmt.pix_mp.num_planes = 1;
    if (v4l2_ioctl(fd, VIDIOC_S_FMT, &coded_format)) {
        fmt::print("*** Setting H.264 encoding: {}\n", strerror(errno));
        exit(1);
    }

    coded_buffers.count = FLAGS_coded_buffers;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &coded_buffers)) {
        fmt::print("*** Creating H.264 buffers: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print("H.264 stream ({} buffers) enabling...\n", coded_buffers.count);
    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &coded_format.type)) {
        fmt::print("*** Enabling H.264 stream: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print("H.264 stream ON\n");

    // Not sending stream data for setup -- hopefully not needed?

    //
    // "CAPTURE" (device-to-app, i.e. decoder *output*) setup
    //

    v4l2_format decoded_format = {};
    decoded_format.type = decoded_buffers.type;
    if (v4l2_ioctl(fd, VIDIOC_G_FMT, &decoded_format)) {
        fmt::print("*** Getting image format: {}\n", strerror(errno));
    } else {
        fmt::print(
            "Image format: {}x{} {:.4s}",
            decoded_format.fmt.pix_mp.width,
            decoded_format.fmt.pix_mp.height,
            (char const*) &decoded_format.fmt.pix_mp.pixelformat
        );
        for (int pi = 0; pi < decoded_format.fmt.pix_mp.num_planes; ++pi) {
            fmt::print(
                " [{}kB]",
                decoded_format.fmt.pix_mp.plane_fmt[pi].sizeimage / 1024
            );
        }
        fmt::print("\n");
    }

    fmt::print("Image selection:");
    v4l2_selection selection = {};
    selection.type = decoded_format.type;
    selection.target = V4L2_SEL_TGT_CROP;
    if (v4l2_ioctl(fd, VIDIOC_G_SELECTION, &selection)) {
        fmt::print(" crop=[{}]", strerror(errno));
    } else {
        fmt::print(
            " crop=({},{})+({}x{})",
            selection.r.left, selection.r.top,
            selection.r.width, selection.r.height
        );
    }
    selection.target = V4L2_SEL_TGT_COMPOSE;
    if (v4l2_ioctl(fd, VIDIOC_G_SELECTION, &selection)) {
        fmt::print(" compose=[{}]", strerror(errno));
    } else {
        fmt::print(
            " compose=({},{})+({}x{})",
            selection.r.left, selection.r.top,
            selection.r.width, selection.r.height
        );
    }
    fmt::print("\n");

    decoded_buffers.count = FLAGS_decoded_buffers;
    if (v4l2_ioctl(fd, VIDIOC_REQBUFS, &decoded_buffers)) {
        fmt::print("*** Creating decoded buffers: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print("Image stream ({} frames) enabling...\n", decoded_buffers.count);
    if (v4l2_ioctl(fd, VIDIOC_STREAMON, &decoded_format.type)) {
        fmt::print("*** Enabling decoded stream: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print("Image stream ON\n");
}

void decode_media(const std::string& filename) {
    AVFormatContext* av_context = nullptr;
    AVStream* av_stream = nullptr;
    open_media_h264_stream(filename, &av_context, &av_stream);
    auto const* av_codec = av_stream->codecpar;

    int decoder_fd = -1;
    open_v4l2_h264_decoder(&decoder_fd);
    setup_decoder(decoder_fd, av_codec->width, av_codec->height);

    //
    //
    //

    v4l2_close(decoder_fd);
    avformat_close_input(&av_context);
}

DEFINE_string(media, "", "Media file or URL to decode");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_media.empty()) {
        fmt::print("*** Usage: pivid_inspect_avformat --media=<mediafile>\n");
        exit(1);
    }

    decode_media(FLAGS_media);
    return 0;
}
