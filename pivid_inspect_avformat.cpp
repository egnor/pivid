// Simple command line tool to list media files and their contents.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cmath>
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
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

#include "image_buffer.h"

void av_or_die(std::string const& text, int const err) {
    if (err < 0) {
        char errbuf[256];
        av_strerror(err, errbuf, sizeof(errbuf));
        fmt::print("*** {}: {}\n", text, errbuf);
        exit(1);
    }
}

void inspect_media(AVFormatContext* const avc) {
    av_or_die("Stream info", avformat_find_stream_info(avc, nullptr));

    fmt::print("=== {} ===\n", avc->url);
    fmt::print("Container:");
    if (avc->start_time > 0)
        fmt::print(" {:.3f} +", avc->start_time * 1.0 / AV_TIME_BASE);
    if (avc->duration > 0)
        fmt::print(" {:.1f}sec", avc->duration * 1.0 / AV_TIME_BASE);
    if (avc->bit_rate > 0)
        fmt::print(" {}bps", avc->bit_rate);
    fmt::print(" ({})\n", avc->iformat->name);

    if (avc->iformat->flags) {
        fmt::print("   ");
        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if ((avc->iformat->flags & bit)) {
                switch (bit) {
#define F(X) case AVFMT_##X: fmt::print(" {}", #X); break
                    F(NOFILE);
                    F(NEEDNUMBER);
                    F(SHOW_IDS);
                    F(GLOBALHEADER);
                    F(NOTIMESTAMPS);
                    F(GENERIC_INDEX);
                    F(TS_DISCONT);
                    F(VARIABLE_FPS);
                    F(NODIMENSIONS);
                    F(NOSTREAMS);
                    F(NOBINSEARCH);
                    F(NOGENSEARCH);
                    F(NO_BYTE_SEEK);
                    F(ALLOW_FLUSH);
                    F(TS_NONSTRICT);
                    F(TS_NEGATIVE);
                    F(SEEK_TO_PTS);
#undef F
                    default: fmt::print(" ?flag=0x{:x}?", bit); break;
                }
            }
        }
        fmt::print("\n");
    }

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

        fmt::print("    S{}", stream->id);
        if (stream->start_time > 0)
            fmt::print(" {:.3f} +", stream->start_time * time_base);
        if (stream->duration > 0)
            fmt::print(" {:.1f}s", stream->duration * time_base);
        if (stream->nb_frames > 0)
            fmt::print(" {}f", stream->nb_frames);
        if (stream->nb_index_entries > 0)
            fmt::print(" {}ix", stream->nb_index_entries);
        if (stream->avg_frame_rate.num > 0)
            fmt::print(" {:.3f}fps", av_q2d(stream->avg_frame_rate));
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
            if (par->codec_id)
                fmt::print(" ({})", avcodec_get_name(par->codec_id));
            if (par->bit_rate)
                fmt::print(" {}bps", par->bit_rate);
            if (par->width || par->height)
                fmt::print(" {}x{}", par->width, par->height);
            if (par->sample_rate)
                fmt::print(" {}hz", par->sample_rate);
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                auto name = av_get_pix_fmt_name((AVPixelFormat) par->format);
                if (name) fmt::print(" ({})", name);
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

void seek_before(AVFormatContext* const avc, double seconds) {
    int64_t const t = seconds * AV_TIME_BASE;
    fmt::print("Requesting seek before: {:.3f}sec\n\n", t * 1.0 / AV_TIME_BASE);
    av_or_die("Seek", avformat_seek_file(avc, -1, 0, t, t, 0));
}

void seek_after(AVFormatContext* const avc, double seconds) {
    int64_t const t = seconds * AV_TIME_BASE;
    int64_t const max_t = std::max(t, avc->duration) + AV_TIME_BASE;
    fmt::print("Requesting seek after: {:.3f}sec\n\n", t * 1.0 / AV_TIME_BASE);
    av_or_die("Seek", avformat_seek_file(avc, -1, t, t, max_t, 0));
}

void list_packets(AVFormatContext* const avc) {
    AVPacket packet = {};
    fmt::print("--- Frames ---\n");
    while (av_read_frame(avc, &packet) >= 0) {
        auto const* stream = avc->streams[packet.stream_index];
        fmt::print("S{}", avc->streams[packet.stream_index]->id);
        if (stream->codecpar->codec_id)
            fmt::print(" ({})", avcodec_get_name(stream->codecpar->codec_id));

        fmt::print(" {}", pivid::debug_size(packet.size));
        if (packet.pos >= 0)
            fmt::print(" @{:<8d}", packet.pos);

        double const time_base = av_q2d(stream->time_base);
        if (packet.pts != AV_NOPTS_VALUE)
            fmt::print(" p@{:.3f}s", packet.pts * time_base);
        if (packet.duration != 0)
            fmt::print(" {:+.3f}s", packet.duration * time_base);
        if (packet.dts != AV_NOPTS_VALUE)
            fmt::print(" d@{:.3f}s", packet.dts * time_base);

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

int main(int argc, char** argv) {
    std::string media_file;
    double seek_before = NAN;
    double seek_after = NAN;
    bool debug_libav = false;
    bool list_packets = false;

    CLI::App app("Use libavformat to inspect a media file");
    app.add_option("--media", media_file, "File or URL to inspect")->required();
    app.add_option("--seek_before", seek_before, "Find keyframe before time");
    app.add_option("--seek_after", seek_after, "Find keyframe after time");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    app.add_flag("--list_packets", list_packets, "Print individual frames");
    CLI11_PARSE(app, argc, argv);

    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    AVFormatContext* avc = nullptr;
    av_or_die(
        media_file,
        avformat_open_input(&avc, media_file.c_str(), nullptr, nullptr)
    );

    inspect_media(avc);
    if (!std::isnan(seek_before)) ::seek_before(avc, seek_before);
    if (!std::isnan(seek_after)) ::seek_after(avc, seek_after);
    if (list_packets) ::list_packets(avc);

    avformat_close_input(&avc);
    return 0;
}
