// Simple command line tool to list media files and their contents.

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <map>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/usage.h>
#include <absl/strings/str_format.h>

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
        absl::PrintF("*** Stream info (%s): %s\n", avc->url, strerror(errno));
    }

    absl::PrintF("=== %s ===\n", avc->url);
    absl::PrintF("Container:");
    if (avc->duration)
        absl::PrintF(" %.1fsec", avc->duration * 1.0 / AV_TIME_BASE);
    if (avc->bit_rate)
        absl::PrintF(" %dbps", avc->bit_rate);
    absl::PrintF(" (%s)\n", avc->iformat->long_name);

    AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(
        avc->metadata, "", entry, AV_DICT_IGNORE_SUFFIX
    ))) {
        absl::PrintF("    %s: %s\n", entry->key, entry->value);
    }
    absl::PrintF("\n");

    absl::PrintF("%d stream(s):\n", avc->nb_streams);
    for (uint32_t si = 0; si < avc->nb_streams; ++si) {
        auto const* stream = avc->streams[si];
        double const time_base = av_q2d(stream->time_base);

        absl::PrintF("    Str #%d", stream->id);
        if (stream->duration > 0)
            absl::PrintF(" %.1fsec", stream->duration * time_base);
        if (stream->nb_frames > 0)
            absl::PrintF(" %dfr", stream->nb_frames);
        if (stream->avg_frame_rate.num > 0)
            absl::PrintF(" %.1ffps", av_q2d(stream->avg_frame_rate));
        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if ((stream->disposition & bit)) {
                switch (bit) {
#define D(X) case AV_DISPOSITION_##X: absl::PrintF(" %s", #X); break
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
                    default: absl::PrintF(" ?disp=0x%x?", bit); break;
                }
            }
        }
        if (stream->codecpar) {
            auto const* par = stream->codecpar;
            switch (par->codec_type) {
#define T(X) case AVMEDIA_TYPE_##X: absl::PrintF(" %s", #X); break
                T(UNKNOWN);
                T(VIDEO);
                T(AUDIO);
                T(DATA);
                T(SUBTITLE);
                T(ATTACHMENT);
#undef T
                default: absl::PrintF(" ?type=%d?", par->codec_type); break;
            }
            absl::PrintF(" (%s)", avcodec_get_name(par->codec_id));
            if (par->bit_rate)
                absl::PrintF(" %dbps", par->bit_rate);
            if (par->width || par->height)
                absl::PrintF(" %dx%d", par->width, par->height);
            if (par->sample_rate)
                absl::PrintF(" %dhz", par->sample_rate);
            if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
                auto const pixfmt = (AVPixelFormat) par->format;
                absl::PrintF(" (%s)", av_get_pix_fmt_name(pixfmt));
            }
        }
        absl::PrintF("\n");

        while ((entry = av_dict_get(
            stream->metadata, "", entry, AV_DICT_IGNORE_SUFFIX
        ))) {
            absl::PrintF("        %s: %s\n", entry->key, entry->value);
        }
    }
    absl::PrintF("\n");
}

void list_frames(AVFormatContext* const avc) {
    AVPacket packet = {};
    absl::PrintF("--- Frames ---\n");
    if (avformat_seek_file(avc, -1, 0, 0, 0, 0) < 0) {
        absl::PrintF("*** Seek to start (%s): %s\n", avc->url, strerror(errno));
        exit(1);
    }
    while (av_read_frame(avc, &packet) >= 0) {
        auto const* stream = avc->streams[packet.stream_index];
        absl::PrintF(
            "S%d (%s) %4dkB",
            packet.stream_index,
            avcodec_get_name(stream->codecpar->codec_id),
            packet.size / 1024
        );

        if (packet.pos >= 0)
            absl::PrintF(" @%-8d", packet.pos);

        double const time_base = av_q2d(stream->time_base);
        if (packet.pts != AV_NOPTS_VALUE)
            absl::PrintF(" pres@%.3fs", packet.pts * time_base);
        if (packet.duration != 0)
            absl::PrintF(" len=%.3fs", packet.duration * time_base);
        if (packet.dts != AV_NOPTS_VALUE)
            absl::PrintF(" deco@%.3fs", packet.dts * time_base);

        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if ((packet.flags & bit)) {
                switch (bit) {
#define F(X) case AV_PKT_FLAG_##X: absl::PrintF(" %s", #X); break
                    F(KEY);
                    F(CORRUPT);
                    F(DISCARD);
                    F(TRUSTED);
                    F(DISPOSABLE);
#undef F
                    default: absl::PrintF(" ?0x%x?", bit); break;
                }
            }
        }

        if (packet.side_data_elems) absl::PrintF(" /");
        for (int si = 0; si < packet.side_data_elems; ++si) {
            switch (packet.side_data[si].type) {
#define S(X) case AV_PKT_DATA_##X: absl::PrintF(" %s", #X); break
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
                default: absl::PrintF(" ?side%d?", packet.side_data[si].type);
            }
        }

        absl::PrintF("\n");
        av_packet_unref(&packet);
    }
    absl::PrintF("\n");
}

void dump_stream(
    AVFormatContext* const avc, AVStream* const stream,
    std::string const& prefix
) {
    auto const filename = absl::StrFormat(
        "%s.%d.%s", prefix, stream->index,
        avcodec_get_name(stream->codecpar->codec_id)
    );

    absl::PrintF("Dumping stream #%d => %s ...\n", stream->index, filename);
    FILE* const out = fopen(filename.c_str(), "wb");
    if (out == nullptr) {
        absl::PrintF("*** %s: %s\n", filename, strerror(errno));
        exit(1);
    }

    if (avformat_seek_file(avc, stream->index, 0, 0, 0, 0) < 0) {
        absl::PrintF("*** Seek to start (%s): %s\n", avc->url, strerror(errno));
        exit(1);
    }

    AVPacket packet = {};
    while (av_read_frame(avc, &packet) >= 0) {
        if (packet.stream_index != stream->index) continue;
        fwrite(packet.data, packet.size, 1, out);
    }

    fclose(out);
}

ABSL_FLAG(std::string, media, "", "Media file or URL to inspect");

ABSL_FLAG(bool, list_frames, false, "Print individual frames");

ABSL_FLAG(std::string, dump_streams, "", "Prefix to dump raw streams to");

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage("Use libavformat to inspect a media file");
    absl::ParseCommandLine(argc, argv);
    auto const& media_file = absl::GetFlag(FLAGS_media);
    if (media_file.empty()) {
        absl::PrintF("*** No --media=<mediafile> given\n");
        exit(1);
    }

    AVFormatContext* avc = nullptr;
    if (avformat_open_input(&avc, media_file.c_str(), nullptr, nullptr) < 0) {
        absl::PrintF("*** %s: %s\n", media_file, strerror(errno));
        exit(1);
    }

    inspect_media(avc);
    if (absl::GetFlag(FLAGS_list_frames)) list_frames(avc);

    auto const& stream_prefix = absl::GetFlag(FLAGS_dump_streams);
    if (!stream_prefix.empty()) {
        for (uint32_t si = 0; si < avc->nb_streams; ++si) {
            dump_stream(avc, avc->streams[si], stream_prefix);
        }
        absl::PrintF("\n");
    }

    avformat_close_input(&avc);
    return 0;
}
