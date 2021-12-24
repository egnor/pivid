#include "media_decoder.h"

#undef NDEBUG
#include <assert.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

#include <fmt/core.h>

namespace pivid {

namespace {

//
// Libav-specific error handling
//

using strview = std::string_view;

class LibavError : public DecoderError {
  public:
    LibavError(strview action, strview note, strview error, int avcode = 0) {
        what_val.assign(action);
        if (!note.empty()) what_val += fmt::format(" ({})", note);
        if (!error.empty()) what_val += fmt::format(": {}", error);
        if (avcode) {
            char errbuf[256];
            av_strerror(avcode, errbuf, sizeof(errbuf));
            what_val += fmt::format(": {}", errbuf);
        }
    }

    virtual char const* what() const noexcept { return what_val.c_str(); }

  private:
    std::string what_val;
};

int check_av(int avcode, std::string_view action, std::string_view note = "") {
    if (avcode >= 0) return avcode; // No error.
    throw LibavError(action, note, "", avcode);
}

template <typename T>
T* check_alloc(T* item, std::string_view action, std::string_view note = "") {
    if (item) return item;  // No error.
    throw LibavError(action, note, "", AVERROR(ENOMEM));
}

//
// DecodedFrame implementation
//

class LibavDecodedFrame : public DecodedFrame {
  public:
    virtual ~LibavDecodedFrame() noexcept {
        if (av_frame) av_frame_free(&av_frame);
    }

    virtual AVFrame const& frame() { return *av_frame; }

    virtual AVDRMFrameDescriptor const& drm() {
        return *(AVDRMFrameDescriptor const*)(av_frame->data[0]);
    }

    void init(AVFrame** const frame) {
        assert(frame && *frame);
        assert((*frame)->format == AV_PIX_FMT_DRM_PRIME);
        assert((*frame)->data[0] && !(*frame)->data[1]);
        assert(!this->av_frame);
        std::swap(this->av_frame, *frame);  // Take ownership
    }

  private:
    AVFrame* av_frame = nullptr;
};

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

    virtual AVStream const& stream() {
        assert(format_context && stream_index >= 0);
        return *format_context->streams[stream_index];
    }

    virtual bool at_eof() const {
        return eof_seen_from_codec;
    }

    virtual std::unique_ptr<DecodedFrame> next_frame() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc(), "Packet");
        if (!av_frame) av_frame = check_alloc(av_frame_alloc(), "Frame");

        for (;;) {
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
                    continue;
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

            if (!eof_seen_from_codec) {
                auto const err = avcodec_receive_frame(codec_context, av_frame);
                if (err == AVERROR_EOF) {
                    eof_seen_from_codec = true;
                } else if (err != AVERROR(EAGAIN)) {
                    check_av(err, "Decode frame", codec_context->codec->name);
                    auto frame = std::make_unique<LibavDecodedFrame>();
                    frame->init(&av_frame);
                    return frame;
                }
            }

            return nullptr;  // Busy or EOF
        }
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
            throw LibavError("Decode", url, "Codec not found");
        } else if (codec->id == AV_CODEC_ID_H264) {
            auto* m2m_codec = avcodec_find_decoder_by_name("h264_v4l2m2m");
            if (m2m_codec) codec = m2m_codec;
        }

        codec_context = avcodec_alloc_context3(codec);
        check_alloc(codec_context, "Codec context");
        check_av(
            avcodec_parameters_to_context(
                codec_context, format_context->streams[stream_index]->codecpar
            ), "Codec parameters", codec->name
        );

        codec_context->get_format = pixel_format_callback;
        check_av(
            avcodec_open2(codec_context, codec, nullptr),
            "Open codec", codec->name
        );
    }

  private:
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    int stream_index = -1;

    AVPacket* av_packet = nullptr;
    AVFrame* av_frame = nullptr;
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
