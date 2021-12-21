#include "media_decoder.h"

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

class LibavMediaError : public MediaError {
  public:
    LibavMediaError(std::string const& text) : text(text) {}
    virtual char const* what() const noexcept { return text.c_str(); }
  private:
    std::string text;
};

[[noreturn]] void throw_error(
    std::string_view action, std::string_view note, std::string_view error
) {
    std::string text{action};
    if (!note.empty()) text += fmt::format(" ({})", note);
    if (!error.empty()) text += fmt::format(": {}", error);
    throw LibavMediaError(text);
}

int check_av(
    std::string_view action, std::string_view note, int const avcode
) {
    if (avcode >= 0) return avcode; // No error.
    char errbuf[256];
    av_strerror(avcode, errbuf, sizeof(errbuf));
    throw_error(action, note, errbuf);
}

template <typename T>
T* check_alloc(std::string_view action, T* item) {
    if (item) return item;  // No error.
    throw_error(action, "", "Allocation failed");
}

//
// MediaFrame implementation
//

class LibavMediaFrame : public MediaFrame {
  public:
    virtual ~LibavMediaFrame() noexcept {
        if (av_frame) av_frame_free(&av_frame);
    }

    virtual AVFrame const& frame() { return *av_frame; }

    virtual AVDRMFrameDescriptor const& drm() {
        return *(AVDRMFrameDescriptor const*)(av_frame->data[0]);
    }

    void init(AVFrame** const frame) {
        assert(frame && *frame);
        assert(frame->format == AV_PIX_FMT_DRM_PRIME);
        assert(frame->data[0] && !frame->data[1]);
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

    virtual std::unique_ptr<MediaFrame> next_frame() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc("Packet", av_packet_alloc());
        if (!av_frame) av_frame = check_alloc("Frame", av_frame_alloc());

        for (;;) {
            if (!eof_seen_from_file) {
                if (av_packet->data == nullptr) {
                    auto const err = av_read_frame(format_context, av_packet);
                    if (err == AVERROR_EOF) {
                        eof_seen_from_file = true;
                        continue;
                    }

                    check_av("Read", format_context->iformat->name, err);
                    if (av_packet->stream_index != stream_index) {
                        av_packet_unref(av_packet);
                        continue;
                    }
                }

                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err != AVERROR(EAGAIN)) {
                    check_av("Decode packet", codec_context->codec->name, err);
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
                        check_av("Decode EOF", codec_context->codec->name, err);
                    eof_sent_to_codec = true;
                }
            }

            if (!eof_seen_from_codec) {
                auto const err = avcodec_receive_frame(codec_context, av_frame);
                if (err == AVERROR_EOF) {
                    eof_seen_from_codec = true;
                } else if (err != AVERROR(EAGAIN)) {
                    check_av("Decode frame", codec_context->codec->name, err);
                    auto frame = std::make_unique<LibavMediaFrame>();
                    frame->init(&av_frame);
                    return frame;
                }
            }

            return nullptr;  // Busy or EOF
        }
    }

    void init(std::string const& url) {
        check_av(
            "Open media", url,
            avformat_open_input(&format_context, url.c_str(), nullptr, nullptr)
        );

        check_av(
            "Find stream info", url,
            avformat_find_stream_info(format_context, nullptr)
        );

        AVCodec* codec = nullptr;
        stream_index = check_av(
            "Find video stream", url,
            av_find_best_stream(
                format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0
            )
        );

        if (codec == nullptr || stream_index < 0) {
            throw_error("Codec not found", url, "");
        } else if (codec->id == AV_CODEC_ID_H264) {
            char const* m2m_codec = "h264_v4l2m2m";
            codec = avcodec_find_decoder_by_name(m2m_codec);
            if (!codec) throw_error("Codec", m2m_codec, "Not found");
        }

        codec_context = avcodec_alloc_context3(codec);
        check_alloc("Codec context", codec_context);
        check_av(
            "Codec parameters", codec->name,
            avcodec_parameters_to_context(
                codec_context, format_context->streams[stream_index]->codecpar
            )
        );

        codec_context->get_format = pixel_format_callback;
        check_av(
            "Open codec", codec->name,
            avcodec_open2(codec_context, codec, nullptr)
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
