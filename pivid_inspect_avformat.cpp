// Simple command line tool to list media files and their contents.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <map>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

void inspect_media(AVFormatContext* const avc) {
    if (avformat_find_stream_info(avc, nullptr) < 0) {
        fmt::print("*** Stream info ({}): {}\n", avc->url, strerror(errno));
    }

    fmt::print("=== {} ===\n", avc->url);
    fmt::print("Container:");
    if (avc->duration)
        fmt::print(" {:.1f}sec", avc->duration * 1.0 / AV_TIME_BASE);
    if (avc->bit_rate)
        fmt::print(" {}bps", avc->bit_rate);
    fmt::print(" ({})\n", avc->iformat->long_name);

    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(
        avc->metadata, "", entry, AV_DICT_IGNORE_SUFFIX
    ))) {
        fmt::print("    {}: {}\n", entry->key, entry->value);
    }
    fmt::print("\n");

    fmt::print("{} stream(s):\n", avc->nb_streams);
    for (uint32_t si = 0; si < avc->nb_streams; ++si) {
        auto const* stream = avc->streams[si];
        double const time_base = av_q2d(stream->time_base);

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
            auto const* par = stream->codecpar;
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
                auto const pixfmt = (AVPixelFormat) par->format;
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
}

void list_frames(AVFormatContext* const avc) {
    AVPacket packet = {};
    fmt::print("--- Frames ---\n");
    if (avformat_seek_file(avc, -1, 0, 0, 0, 0) < 0) {
        fmt::print("*** Seek to start ({}): {}\n", avc->url, strerror(errno));
        exit(1);
    }
    while (av_read_frame(avc, &packet) >= 0) {
        auto const* stream = avc->streams[packet.stream_index];
        fmt::print(
            "S{} ({}) {:4d}kB",
            packet.stream_index,
            avcodec_get_name(stream->codecpar->codec_id),
            packet.size / 1024
        );

        if (packet.pos >= 0)
            fmt::print(" @{:<8d}", packet.pos);

        double const time_base = av_q2d(stream->time_base);
        if (packet.pts != AV_NOPTS_VALUE)
            fmt::print(" pres@{:.3f}s", packet.pts * time_base);
        if (packet.duration != 0)
            fmt::print(" len={:.3f}s", packet.duration * time_base);
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

void dump_stream(
    AVFormatContext* const avc, AVStream* const stream,
    std::string const& prefix
) {
    auto const filename = fmt::format(
        "{}.{}.{}", prefix, stream->index,
        avcodec_get_name(stream->codecpar->codec_id)
    );

    fmt::print("Dumping stream #{} => {} ...\n", stream->index, filename);
    FILE* const out = fopen(filename.c_str(), "wb");
    if (out == nullptr) {
        fmt::print("*** {}: {}\n", filename, strerror(errno));
        exit(1);
    }

    if (avformat_seek_file(avc, stream->index, 0, 0, 0, 0) < 0) {
        fmt::print("*** Seek to start ({}): {}\n", avc->url, strerror(errno));
        exit(1);
    }

    AVPacket packet = {};
    while (av_read_frame(avc, &packet) >= 0) {
        if (packet.stream_index != stream->index) continue;
        fwrite(packet.data, packet.size, 1, out);
    }

    fclose(out);
}

int main(int argc, char** argv) {
    std::string media_file;
    bool list_frames = false;
    std::string dump_prefix;

    CLI::App app("Use libavformat to inspect a media file");
    app.add_option("--media", media_file, "File or URL to inspect")->required();
    app.add_option("--list_frames", list_frames, "Print individual frames");
    app.add_option("--dump_streams", dump_prefix, "Prefix for raw stream dump");
    CLI11_PARSE(app, argc, argv);

    AVFormatContext* avc = nullptr;
    if (avformat_open_input(&avc, media_file.c_str(), nullptr, nullptr) < 0) {
        fmt::print("*** {}: {}\n", media_file, strerror(errno));
        exit(1);
    }

    inspect_media(avc);
    if (list_frames) ::list_frames(avc);

    if (!dump_prefix.empty()) {
        for (uint32_t si = 0; si < avc->nb_streams; ++si) {
            dump_stream(avc, avc->streams[si], dump_prefix);
        }
        fmt::print("\n");
    }

    avformat_close_input(&avc);
    return 0;
}
