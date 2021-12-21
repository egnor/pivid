#include "media_decoder.h"

#include <fmt/core.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

namespace pivid {

namespace {

class LibavMediaError : public MediaError {
  public:
    LibavMediaError(std::string_view const& text) : text(text) {}
    LibavMediaError(std::string_view const& action, int const code) {
        char errbuf[256];
        av_strerror(code, errbuf, sizeof(errbuf));
        text = fmt::format("{}: {}", action, errbuf);
    }

    virtual char const* what() const noexcept { return text.c_str(); }

  private:
    std::string text;
};

int check_av(std::string_view const& action, int const code) {
    if (code < 0) throw LibavMediaError(action, code);
    return code;
}

class LibavMediaFrame : public MediaFrame {
  public:
};

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
        if (codec_context) avcodec_free_context(&codec_context);
        if (format_context) avformat_close_input(&format_context);
    }

    virtual std::unique_ptr<MediaFrame> next_frame() {
        return nullptr;
    }

    void init(std::string const& url) {
        check_av(url, avformat_open_input(
            &format_context, url.c_str(), nullptr, nullptr
        ));

        check_av(url, avformat_find_stream_info(format_context, nullptr));

        AVCodec* codec;
        int const stream_index = check_av(url, av_find_best_stream(
            format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0
        ));

        if (codec == nullptr) {
            throw LibavMediaError(fmt::format("Codec not found: {}", url));
        } else if (codec->id == AV_CODEC_ID_H264) {
            codec = avcodec_find_decoder_by_name("h264_v4l2m2m");
            if (!codec) throw LibavMediaError("No \"h264_v4l2m2m\" codec");
        }

        codec_context = avcodec_alloc_context3(codec);
        if (!codec_context) check_av(
            fmt::format("Codec context ({})", codec->name),
            AVERROR(ENOMEM)
        );
        check_av(
            fmt::format("Codec parameters ({})", codec->name),
            avcodec_parameters_to_context(
                codec_context, format_context->streams[stream_index]->codecpar
            )
        );

        codec_context->get_format = pixel_format_callback;
        check_av(
            fmt::format("Opening codec ({})", codec->name),
            avcodec_open2(codec_context, codec, nullptr)
        );
    }

  private:
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
};

}  // anonymous namespace

std::unique_ptr<MediaDecoder> new_media_decoder(const std::string& url) {
    auto decoder = std::make_unique<LibavMediaDecoder>();
    decoder->init(url);
    return decoder;
}

}  // namespace pivid
