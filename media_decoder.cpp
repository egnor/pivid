#include "media_decoder.h"

#undef NDEBUG
#include <assert.h>

#include <system_error>

#include <fmt/core.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/pixdesc.h>
}

namespace pivid {

namespace {

//
// Libav-specific error handling
//

using strview = std::string_view;

class LibavErrorCategory : public std::error_category {
  public:
    virtual char const* name() const noexcept { return "libav"; }
    virtual std::string message(int code) const {
        char errbuf[256];
        av_strerror(code, errbuf, sizeof(errbuf));
        return errbuf;
    }
};

LibavErrorCategory const& libav_category() {
    static LibavErrorCategory singleton;
    return singleton;
}

int check_av(int avcode, strview note, strview detail) {
    if (avcode >= 0) return avcode; // No error.
    auto what = fmt::format("{} ({})", note, detail);
    throw std::system_error(avcode, libav_category(), what);
}

template <typename T>
T* check_alloc(T* item) {
    if (item) return item;  // No error.
    throw std::bad_alloc();
}

//
// MediaDecoder implementation
//

extern "C" AVPixelFormat pixel_format_callback(
    AVCodecContext* context, AVPixelFormat const* formats
) {
    for (auto const* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        if (*f == AV_PIX_FMT_NONE) return *f;  // None match.
        if (*f == AV_PIX_FMT_DRM_PRIME) break;
    }
    if (av_hwdevice_ctx_create(
        &context->hw_device_ctx, AV_HWDEVICE_TYPE_DRM, nullptr, nullptr, 0
    ) < 0) {
        return AV_PIX_FMT_NONE;
    }
    return AV_PIX_FMT_DRM_PRIME;
}

class LibavMediaDecoder : public MediaDecoder {
  public:
    virtual ~LibavMediaDecoder() noexcept {
        if (av_frame) av_frame_free(&av_frame);
        if (av_packet) av_packet_free(&av_packet);
        if (codec_context) avcodec_free_context(&codec_context);
        if (format_context) avformat_close_input(&format_context);
    }

    virtual MediaInfo const& info() const { return media_info; }

    virtual bool next_frame_ready() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc());
        if (!av_frame) av_frame = check_alloc(av_frame_alloc());

        for (;;) {
            if (eof_seen_from_codec) return true;
            if (av_frame->width && av_frame->height) return true;

            if (!eof_seen_from_file) {
                if (av_packet->data == nullptr) {
                    auto const err = av_read_frame(format_context, av_packet);
                    if (err == AVERROR_EOF) {
                        eof_seen_from_file = true;
                        continue;
                    }

                    check_av(err, "Read", format_context->iformat->name);
                    if (av_packet->stream_index != stream_index) {
                        av_packet_unref(av_packet);
                        continue;
                    }
                }

                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err != AVERROR(EAGAIN)) {
                    check_av(err, "Decode packet", codec_context->codec->name);
                    av_packet_unref(av_packet);
                    assert(av_packet->data == nullptr);
                    continue;  // Get more packets
                }
            }

            if (eof_seen_from_file && !eof_sent_to_codec) {
                assert(av_packet->data == nullptr);
                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err != AVERROR(EAGAIN)) {
                    if (err != AVERROR_EOF)
                        check_av(err, "Decode EOF", codec_context->codec->name);
                    eof_sent_to_codec = true;
                }
            }

            auto const err = avcodec_receive_frame(codec_context, av_frame);
            if (err == AVERROR(EAGAIN)) return false;
            if (err == AVERROR_EOF) {
                eof_seen_from_codec = true;
                continue;
            }

            check_av(err, "Decode frame", codec_context->codec->name);
            assert(av_frame->format == AV_PIX_FMT_DRM_PRIME);
            assert(av_frame->data[0] && !av_frame->data[1]);
            assert(av_frame->width && av_frame->height);
        }
    }

    virtual MediaFrame next_frame() {
        MediaFrame decoded = {};
        if (eof_seen_from_codec) {
            decoded.at_eof = true;
            decoded.time = last_frame_time;
            return decoded;
        }

        if (!av_frame || !av_frame->width || !av_frame->height)
            throw std::logic_error("Frame not ready");

        auto const deleter = [](AVFrame* f) mutable {av_frame_free(&f);};
        std::shared_ptr<AVFrame> shared_frame{av_frame, deleter};
        av_frame = nullptr;  // Owned by shared_frame now.

        auto const* stream = format_context->streams[stream_index];
        decoded.time = shared_frame->pts * av_q2d(stream->time_base);
        decoded.is_corrupt = (shared_frame->flags & AV_FRAME_FLAG_CORRUPT);
        decoded.is_key_frame = shared_frame->key_frame;
        switch (shared_frame->pict_type) {
            case AV_PICTURE_TYPE_NONE: break;
#define P(x) case AV_PICTURE_TYPE_##x: decoded.frame_type = #x; break
            P(I);
            P(P);
            P(B);
            P(S);
            P(SI);
            P(SP);
            P(BI);
#undef P
            default: decoded.frame_type = "?";
        }

        auto av_drm = (AVDRMFrameDescriptor const*)(shared_frame->data[0]);
        for (int l = 0; l < av_drm->nb_layers; ++l) {
            auto const& av_layer = av_drm->layers[l];

            // Bundle the shared AVFrame with a FrameBuffer into the shared_ptr.
            using FbPair = std::pair<std::shared_ptr<AVFrame>, FrameBuffer>;
            auto ptr_fb = std::make_shared<FbPair>(shared_frame, FrameBuffer{});
            auto fb = std::shared_ptr<FrameBuffer>(ptr_fb, &ptr_fb->second);
            fb->fourcc = av_layer.format;
            fb->width = shared_frame->width;
            fb->height = shared_frame->height;

            for (int p = 0; p < av_layer.nb_planes; ++p) {
                auto const& av_plane = av_layer.planes[p];
                auto const& av_obj = av_drm->objects[av_plane.object_index];
                fb->modifier = av_obj.format_modifier;
                fb->channels.push_back({
                    .dma_fd = av_obj.fd,
                    .start_offset = av_plane.offset,
                    .bytes_per_line = av_plane.pitch
                });
            }

            decoded.layers.push_back(std::move(fb));
        }

        last_frame_time = decoded.time;
        return decoded;
    }

    void init(std::string const& url) {
        check_av(
            avformat_open_input(&format_context, url.c_str(), nullptr, nullptr),
            "Open media", url
        );

        check_av(
            avformat_find_stream_info(format_context, nullptr),
            "Find stream info", url
        );

        AVCodec* codec = nullptr;
        stream_index = check_av(
            av_find_best_stream(
                format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0
            ), "Find video stream", url
        );

        if (codec == nullptr || stream_index < 0) {
            throw std::system_error(
                AVERROR_DECODER_NOT_FOUND, libav_category(), url
            );
        } else if (codec->id == AV_CODEC_ID_H264) {
            auto* m2m_codec = avcodec_find_decoder_by_name("h264_v4l2m2m");
            if (m2m_codec) codec = m2m_codec;
        }

        assert(stream_index >= 0);
        auto const* str = format_context->streams[stream_index];
        codec_context = check_alloc(avcodec_alloc_context3(codec));
        check_av(
            avcodec_parameters_to_context(codec_context, str->codecpar),
            "Codec parameters", codec->name
        );

        codec_context->get_format = pixel_format_callback;
        check_av(
            avcodec_open2(codec_context, codec, nullptr),
            "Open codec", codec->name
        );

        double const time_base = av_q2d(str->time_base);
        media_info.container_type = format_context->iformat->long_name;
        media_info.codec_name = codec_context->codec->long_name;
        media_info.pixel_format = av_get_pix_fmt_name(codec_context->pix_fmt);
        media_info.start_time =
            (str->start_time > 0) ? str->start_time * time_base :
            (format_context->start_time > 0) ?
                format_context->start_time * 1.0 / AV_TIME_BASE : 0;
        media_info.duration =
            (str->duration > 0) ? str->duration * time_base :
            (format_context->duration > 0) ?
                format_context->duration * 1.0 / AV_TIME_BASE : 0;
        media_info.bit_rate =
            (codec_context->bit_rate > 0) ? codec_context->bit_rate :
            (format_context->bit_rate > 0) ? format_context->bit_rate : 0;
        media_info.frame_rate =
            (str->avg_frame_rate.num > 0) ? av_q2d(str->avg_frame_rate) : 0;
        media_info.frame_count = (str->nb_frames > 0) ? str->nb_frames : 0;
        if (codec_context->width > 0 && codec_context->height > 0) {
            media_info.width = codec_context->width;
            media_info.height = codec_context->height;
        }
    }

  private:
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    int stream_index = -1;

    MediaInfo media_info = {};

    AVPacket* av_packet = nullptr;
    AVFrame* av_frame = nullptr;
    double last_frame_time = 0.0;
    bool eof_seen_from_file = false;
    bool eof_sent_to_codec = false;
    bool eof_seen_from_codec = false;
};

}  // anonymous namespace

std::unique_ptr<MediaDecoder> new_media_decoder(const std::string& url) {
    auto decoder = std::make_unique<LibavMediaDecoder>();
    decoder->init(url);
    return decoder;
}

}  // namespace pivid
