#include "media_decoder.h"

#undef NDEBUG
#include <assert.h>
#include <sys/mman.h>

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

class LibavDrmBuffer : public MemoryBuffer {
  public:
    void init(std::shared_ptr<AVDRMFrameDescriptor const> av_drm, int index) {
        auto const* obj = &av_drm->objects[index];
        av_obj = std::shared_ptr<const AVDRMObjectDescriptor>{av_drm, obj};
    }

    ~LibavDrmBuffer() { if (mem) munmap(mem, av_obj->size); }
    virtual int dma_fd() const { return av_obj->fd; }
    virtual size_t buffer_size() const { return av_obj->size; }

    virtual uint8_t* mapped() {
        if (!mem) {
            int const prot = PROT_READ|PROT_WRITE, flags = MAP_SHARED;
            mem = ::mmap(nullptr, av_obj->size, prot, flags, av_obj->fd, 0);
            if (!mem)
                throw std::system_error(errno, std::system_category(), "Mmap");
        }
        return (uint8_t*) mem;
    }

  private:
    std::shared_ptr<AVDRMObjectDescriptor const> av_obj;
    void* mem = nullptr;
};

class LibavMediaDecoder : public MediaDecoder {
  public:
    virtual ~LibavMediaDecoder() noexcept {
        if (av_frame) av_frame_free(&av_frame);
        if (av_packet) av_packet_free(&av_packet);
        if (codec_context) avcodec_free_context(&codec_context);
        if (format_context) avformat_close_input(&format_context);
    }

    virtual MediaInfo const& info() const { return media_info; }

    virtual bool reached_eof() {
        if (!eof_seen_from_codec) next_frame_ready();
        return eof_seen_from_codec;
    }

    virtual bool next_frame_ready() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc());
        if (!av_frame) av_frame = check_alloc(av_frame_alloc());

        for (;;) {
            if (eof_seen_from_codec) return false;
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

    virtual MediaFrame get_next_frame() {
        MediaFrame out = {};
        if (eof_seen_from_codec)
            throw std::logic_error("Read past end of media");

        if (!av_frame || !av_frame->width || !av_frame->height)
            throw std::logic_error("Read unready media frame");

        auto const deleter = [](AVFrame* f) mutable {av_frame_free(&f);};
        std::shared_ptr<AVFrame> av_frame{this->av_frame, deleter};
        this->av_frame = nullptr;  // Owned by shared_ptr now.

        auto const* stream = format_context->streams[stream_index];
        out.time = av_frame->pts * av_q2d(stream->time_base);
        out.is_corrupt = (av_frame->flags & AV_FRAME_FLAG_CORRUPT);
        out.is_key_frame = av_frame->key_frame;
        switch (av_frame->pict_type) {
            case AV_PICTURE_TYPE_NONE: break;
#define P(x) case AV_PICTURE_TYPE_##x: out.frame_type = #x; break
            P(I);
            P(P);
            P(B);
            P(S);
            P(SI);
            P(SP);
            P(BI);
#undef P
            default: out.frame_type = "?";
        }

        std::shared_ptr<AVDRMFrameDescriptor const> av_drm{
            av_frame, (AVDRMFrameDescriptor const*) av_frame->data[0]
        };

        std::vector<std::shared_ptr<LibavDrmBuffer>> buffers;
        for (int o = 0; o < av_drm->nb_objects; ++o) {
            buffers.push_back(std::make_shared<LibavDrmBuffer>());
            buffers.back()->init(av_drm, o);
        }

        for (int l = 0; l < av_drm->nb_layers; ++l) {
            auto const& av_layer = av_drm->layers[l];
            ImageBuffer image = {};
            image.fourcc = av_layer.format;
            image.width = av_frame->width;
            image.height = av_frame->height;

            for (int p = 0; p < av_layer.nb_planes; ++p) {
                auto const& av_plane = av_layer.planes[p];
                auto const& av_obj = av_drm->objects[av_plane.object_index];
                image.modifier = av_obj.format_modifier;
                image.channels.push_back({
                    .memory = buffers[av_plane.object_index],
                    .memory_offset = av_plane.offset,
                    .line_stride = av_plane.pitch
                });
            }

            out.layers.push_back(std::move(image));
        }

        return out;
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
        media_info.container_type = format_context->iformat->name;
        media_info.codec_name = codec_context->codec->name;
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
    bool eof_seen_from_file = false;
    bool eof_sent_to_codec = false;
    bool eof_seen_from_codec = false;

    static AVPixelFormat pixel_format_callback(
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
};

}  // anonymous namespace

std::unique_ptr<MediaDecoder> new_media_decoder(const std::string& url) {
    auto decoder = std::make_unique<LibavMediaDecoder>();
    decoder->init(url);
    return decoder;
}

}  // namespace pivid
