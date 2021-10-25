// Simple command line tool to exercise h.264 decoding via V4L2

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

void decode_media(const std::string &media) {
    //
    // Open media file with libavformat; find H.264 video stream
    //

    AVFormatContext *context = nullptr;
    if (avformat_open_input(&context, media.c_str(), nullptr, nullptr) < 0) {
        fmt::print("*** Error opening: {}\n", media);
        exit(1);
    }

    if (avformat_find_stream_info(context, nullptr) < 0) {
        fmt::print("*** {}: Error reading stream info\n", media);
        exit(1);
    }

    fmt::print("Opened media file: {}\n", media);

    AVStream const* stream = nullptr;
    for (uint32_t si = 0; si < context->nb_streams; ++si) {
        auto const* maybe = context->streams[si];
        if (maybe->codecpar && maybe->codecpar->codec_id == AV_CODEC_ID_H264) {
            stream = maybe;
            break;
        }
    }

    if (!stream) {
        fmt::print("*** {}: No H.264 stream found\n", media);
        exit(1);
    }

    // double const time_base = av_q2d(stream->time_base);
    const auto* const par = stream->codecpar;

    fmt::print(
        "Found H.264 stream (#{}): {}x{} {:.1f}fps\n",
        stream->index, par->width, par->height,
        av_q2d(stream->avg_frame_rate)
    );

    //
    // Scan V4L2 devices and find H.264 decoder
    //
    
    std::filesystem::path const dev_dir = "/dev";
    for (const auto& entry : std::filesystem::directory_iterator(dev_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 5) == "video" && isdigit(filename[5])) {
            int const fd = v4l2_open(entry.path().c_str(), O_RDWR);
            if (fd < 0) {
                fmt::print("*** Error opening: {}\n", entry.path().native());
                continue;
            }
        }
    }

    avformat_close_input(&context);
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
