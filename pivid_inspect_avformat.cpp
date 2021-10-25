// Simple command line tool to list media files and their contents.

#include <fcntl.h>
#include <unistd.h>

#include <map>

#include <fmt/core.h>
#include <gflags/gflags.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

DEFINE_bool(frames, false, "Print individual frames");

void inspect_media(const std::string& filename) {
    AVFormatContext* context = nullptr;
    if (avformat_open_input(&context, filename.c_str(), nullptr, nullptr) < 0) {
        fmt::print("*** Error opening: {}\n", filename);
        exit(1);
    }

    if (avformat_find_stream_info(context, nullptr) < 0) {
        fmt::print("*** {}: Error reading stream info\n", filename);
    }

    fmt::print("=== {} ===\n", context->url);
    fmt::print("Container:");
    if (context->duration)
        fmt::print(" {:.1f}sec", context->duration * 1.0 / AV_TIME_BASE);
    if (context->bit_rate)
        fmt::print(" {}bps", context->bit_rate);
    fmt::print(" ({})\n", context->iformat->long_name);

    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(
        context->metadata, "", entry, AV_DICT_IGNORE_SUFFIX
    ))) {
        fmt::print("    {}: {}\n", entry->key, entry->value);
    }
    fmt::print("\n");

    fmt::print("{} stream(s):\n", context->nb_streams);
    for (uint32_t si = 0; si < context->nb_streams; ++si) {
        auto const* stream = context->streams[si];
        const double time_base = av_q2d(stream->time_base);

        fmt::print("    Str #{}", stream->id);
        if (stream->duration > 0)
            fmt::print(" {:.1f}sec", stream->duration * time_base);
        if (stream->nb_frames > 0)
            fmt::print(" {}fr", stream->nb_frames);
        if (stream->avg_frame_rate.num > 0)
            fmt::print(" {:.1f}fps", av_q2d(stream->avg_frame_rate));
        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if ((stream->disposition & bit)) {
                switch (bit) {
#define D(X) case AV_DISPOSITION_##X: fmt::print(" {}", #X); break
                    D(DEFAULT);
                    D(DUB);
                    D(ORIGINAL);
                    D(COMMENT);
                    D(LYRICS);
                    D(KARAOKE);
                    D(FORCED);
                    D(HEARING_IMPAIRED);
                    D(VISUAL_IMPAIRED);
                    D(CLEAN_EFFECTS);
                    D(ATTACHED_PIC);
                    D(TIMED_THUMBNAILS);
                    D(CAPTIONS);
                    D(DESCRIPTIONS);
                    D(METADATA);
                    D(DEPENDENT);
                    D(STILL_IMAGE);
#undef D
                    default: fmt::print(" ?disp=0x{:x}?", bit); break;
                }
            }
        }
        if (stream->codecpar) {
            const auto* par = stream->codecpar;
            switch (par->codec_type) {
#define T(X) case AVMEDIA_TYPE_##X: fmt::print(" {}", #X); break
                T(UNKNOWN);
                T(VIDEO);
                T(AUDIO);
                T(DATA);
                T(SUBTITLE);
                T(ATTACHMENT);
#undef T
                default: fmt::print(" ?type=%d?", par->codec_type); break;
            }
            fmt::print(" ({})", avcodec_get_name(par->codec_id));
            if (par->bit_rate)
                fmt::print(" {}bps", par->bit_rate);
            if (par->width || par->height)
                fmt::print(" {}x{}", par->width, par->height);
            if (par->sample_rate)
                fmt::print(" {}hz", par->sample_rate);
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                const auto pixfmt = (AVPixelFormat) par->format;
                fmt::print(" ({})", av_get_pix_fmt_name(pixfmt));
            }
        }
        fmt::print("\n");

        while ((entry = av_dict_get(
            stream->metadata, "", entry, AV_DICT_IGNORE_SUFFIX
        ))) {
            fmt::print("        {}: {}\n", entry->key, entry->value);
        }
    }
    fmt::print("\n");

    AVPacket packet = {};
    if (FLAGS_frames) {
        fmt::print("--- Frames ---\n");
        while (av_read_frame(context, &packet) >= 0) {
            const auto* stream = context->streams[packet.stream_index];
            fmt::print(
                "S{} ({})",
                packet.stream_index,
                avcodec_get_name(stream->codecpar->codec_id)
            );
            const double time_base = av_q2d(stream->time_base);
            fmt::print(" {:4d}kB", packet.size / 1024);
            if (packet.duration != 0)
                fmt::print(" {:.3f}s", packet.duration * time_base);
            if (packet.pts != AV_NOPTS_VALUE)
                fmt::print(" show@{:.3f}s", packet.pts * time_base);
            if (packet.dts != AV_NOPTS_VALUE)
                fmt::print(" deco@{:.3f}s", packet.dts * time_base);

            for (uint32_t bit = 1; bit > 0; bit <<= 1) {
                if ((packet.flags & bit)) {
                    switch (bit) {
#define F(X) case AV_PKT_FLAG_##X: fmt::print(" {}", #X); break
                        F(KEY);
                        F(CORRUPT);
                        F(DISCARD);
                        F(TRUSTED);
                        F(DISPOSABLE);
#undef F
                        default: fmt::print(" ?0x{:x}?", bit); break;
                    }
                }
            }

            if (packet.side_data_elems) fmt::print(" /");
            for (int si = 0; si < packet.side_data_elems; ++si) {
                switch (packet.side_data[si].type) {
#define S(X) case AV_PKT_DATA_##X: fmt::print(" {}", #X); break
                    S(PALETTE);
                    S(NEW_EXTRADATA);
                    S(PARAM_CHANGE);
                    S(H263_MB_INFO);
                    S(REPLAYGAIN);
                    S(DISPLAYMATRIX);
                    S(STEREO3D);
                    S(AUDIO_SERVICE_TYPE);
                    S(QUALITY_STATS);
                    S(FALLBACK_TRACK);
                    S(CPB_PROPERTIES);
                    S(SKIP_SAMPLES);
                    S(JP_DUALMONO);
                    S(STRINGS_METADATA);
                    S(SUBTITLE_POSITION);
                    S(MATROSKA_BLOCKADDITIONAL);
                    S(WEBVTT_IDENTIFIER);
                    S(WEBVTT_SETTINGS);
                    S(METADATA_UPDATE);
                    S(MPEGTS_STREAM_ID);
                    S(MASTERING_DISPLAY_METADATA);
                    S(SPHERICAL);
                    S(CONTENT_LIGHT_LEVEL);
                    S(A53_CC);
                    S(ENCRYPTION_INIT_INFO);
                    S(ENCRYPTION_INFO);
                    S(AFD);
#undef S
                    default: fmt::print(" ?side%d?", packet.side_data[si].type);
                }
            }

            fmt::print("\n");
            av_packet_unref(&packet);
        }
        fmt::print("\n");
    }

    avformat_close_input(&context);
}

DEFINE_string(media, "", "Media file or URL to inspect");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (FLAGS_media.empty()) {
        fmt::print("*** Usage: pivid_inspect_avformat --media=<mediafile>\n");
        exit(1);
    }
    inspect_media(FLAGS_media);
    return 0;
}
