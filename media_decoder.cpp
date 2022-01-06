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
// Memory buffer wrappers to AVFrame
//

class LibavDrmFrameMemory : public MemoryBuffer {
  public:
    ~LibavDrmFrameMemory() { if (mem) munmap(mem, av_obj->size); }
    virtual size_t size() const { return av_obj->size; }
    virtual uint8_t* write() { read(); ++writes; return (uint8_t*) mem; }
    virtual int write_count() const { return writes; }
    virtual std::optional<int> dma_fd() const { return {av_obj->fd}; }

    virtual uint8_t const* read() {
        if (!mem) {
            int const prot = PROT_READ|PROT_WRITE, flags = MAP_SHARED;
            mem = ::mmap(nullptr, av_obj->size, prot, flags, av_obj->fd, 0);
            if (!mem)
                throw std::system_error(errno, std::system_category(), "Mmap");
        }
        return (uint8_t const*) mem;
    }

    void init(std::shared_ptr<AVDRMFrameDescriptor const> av_drm, int index) {
        auto const* obj = &av_drm->objects[index];
        av_obj = std::shared_ptr<const AVDRMObjectDescriptor>{
            std::move(av_drm), obj
        };
    }

  private:
    std::shared_ptr<AVDRMObjectDescriptor const> av_obj;
    void* mem = nullptr;
    int writes = 0;
};

class LibavPlainFrameMemory : public MemoryBuffer {
  public:
    virtual size_t size() const { return avf->linesize[index] * avf->height; }
    virtual uint8_t const* read() { return avf->data[index]; }
    virtual uint8_t* write() { ++writes; return avf->data[index]; }
    virtual int write_count() const { return writes; }

    void init(std::shared_ptr<AVFrame> avf, int index) {
        assert(index >= 0 && index < AV_NUM_DATA_POINTERS);
        assert(avf->data[index]);
        this->index = index;
        this->avf = std::move(avf);
    }

  private:
    std::shared_ptr<AVFrame const> avf;
    int index = 0;
    int writes = 0;
};

std::vector<ImageBuffer> layers_from_av_drm(
    std::shared_ptr<AVDRMFrameDescriptor const> av_drm,
    int width, int height
) {
    std::vector<std::shared_ptr<LibavDrmFrameMemory>> buffers;
    for (int o = 0; o < av_drm->nb_objects; ++o) {
        buffers.push_back(std::make_shared<LibavDrmFrameMemory>());
        buffers.back()->init(av_drm, o);
    }

    std::vector<ImageBuffer> out;
    for (int l = 0; l < av_drm->nb_layers; ++l) {
        auto const& av_layer = av_drm->layers[l];
        ImageBuffer image = {};
        image.fourcc = av_layer.format;
        image.width = width;
        image.height = height;

        for (int p = 0; p < av_layer.nb_planes; ++p) {
            auto const& av_plane = av_layer.planes[p];
            auto const& av_obj = av_drm->objects[av_plane.object_index];
            image.modifier = av_obj.format_modifier;
            image.channels.push_back({
                .memory = buffers[av_plane.object_index],
                .memory_offset = av_plane.offset,
                .line_stride = av_plane.pitch,
            });
        }

        out.push_back(std::move(image));
    }
    return out;
}

ImageBuffer image_from_av_plain(std::shared_ptr<AVFrame> av_frame) {
    ImageBuffer out = {};
    out.fourcc = avcodec_pix_fmt_to_codec_tag((AVPixelFormat) av_frame->format);
    out.width = av_frame->width;
    out.height = av_frame->height;
    for (int p = 0; p < AV_NUM_DATA_POINTERS; ++p) {
        if (!av_frame->data[p]) break;
        auto mem = std::make_shared<LibavPlainFrameMemory>();
        mem->init(av_frame, p);
        out.channels.push_back({
            .memory = std::move(mem),
            .memory_offset = 0,
            .line_stride = av_frame->linesize[p],
        });
    }

    return out;
}

MediaFrame frame_from_av(std::shared_ptr<AVFrame> av_frame, double time_base) {
    MediaFrame out = {};
    out.time = av_frame->pts * time_base;
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

    if (av_frame->format == AV_PIX_FMT_DRM_PRIME) {
        assert(av_frame->data[0] && !av_frame->data[1]);
        int const width = av_frame->width, height = av_frame->height;
        auto const* pd = (AVDRMFrameDescriptor const*) av_frame->data[0];
        std::shared_ptr<AVDRMFrameDescriptor const> sd{std::move(av_frame), pd};
        out.layers = layers_from_av_drm(std::move(sd), width, height);
    } else {
        out.layers.push_back(image_from_av_plain(std::move(av_frame)));
    }

    return out;
}

//
// MediaDecoder implementation
//

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
        run_decoder();
        return eof_seen_from_codec;
    }

    virtual bool next_frame_ready() {
        run_decoder();
        return (!eof_seen_from_codec && av_frame->width && av_frame->height);
    }

    virtual MediaFrame get_next_frame() {
        if (eof_seen_from_codec)
            throw std::logic_error("Read past end of media");

        if (!av_frame || !av_frame->width || !av_frame->height)
            throw std::logic_error("Read unready media frame");

        auto const deleter = [](AVFrame* f) mutable {av_frame_free(&f);};
        std::shared_ptr<AVFrame> av_frame{this->av_frame, deleter};
        this->av_frame = nullptr;  // Owned by shared_ptr now.

        auto const* stream = format_context->streams[stream_index];
        return frame_from_av(std::move(av_frame), av_q2d(stream->time_base));
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

        if (codec_context->width > 0 && codec_context->height > 0) {
            media_info.width = codec_context->width;
            media_info.height = codec_context->height;
        }

        if (str->duration > 0) {
            media_info.duration = str->duration * time_base;
        } else if (format_context->duration > 0) {
            media_info.duration = format_context->duration * 1.0 / AV_TIME_BASE;
        }

        if (str->avg_frame_rate.num > 0)
            media_info.frame_rate = av_q2d(str->avg_frame_rate);

        if (codec_context->bit_rate > 0) {
            media_info.bit_rate = codec_context->bit_rate;
        } else if (format_context->bit_rate > 0) {
            media_info.bit_rate = format_context->bit_rate;
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

    void run_decoder() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc());
        if (!av_frame) av_frame = check_alloc(av_frame_alloc());

        while (!eof_seen_from_file) {
            if (av_packet->data == nullptr) {
                auto const err = av_read_frame(format_context, av_packet);
                if (err == AVERROR_EOF) {
                    eof_seen_from_file = true;
                    continue;
                } else {
                    check_av(err, "Read", format_context->iformat->name);
                    if (av_packet->stream_index != stream_index) {
                        av_packet_unref(av_packet);
                        assert(av_packet->data == nullptr);
                        continue;
                    }
                }
            }

            auto const err = avcodec_send_packet(codec_context, av_packet);
            if (err == AVERROR(EAGAIN)) break;
            check_av(err, "Decode packet", codec_context->codec->name);
            av_packet_unref(av_packet);
            assert(av_packet->data == nullptr);
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

        if (!eof_seen_from_codec && !av_frame->width) {
            auto const err = avcodec_receive_frame(codec_context, av_frame);
            if (err == AVERROR_EOF) {
                eof_seen_from_codec = true;
            } else if (err != AVERROR(EAGAIN)) {
                check_av(err, "Decode frame", codec_context->codec->name);
            }
        }
    }

    static AVPixelFormat pixel_format_callback(
        AVCodecContext* context, AVPixelFormat const* formats
    ) {
        for (auto const* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f != AV_PIX_FMT_DRM_PRIME) continue;
            if (av_hwdevice_ctx_create(
                &context->hw_device_ctx, AV_HWDEVICE_TYPE_DRM,
                nullptr, nullptr, 0
            ) < 0) {
                break;
            }
            return AV_PIX_FMT_DRM_PRIME;
        }
        return context->sw_pix_fmt;  // Fall back to non-DRM output.
    }
};

}  // anonymous namespace

std::unique_ptr<MediaDecoder> new_media_decoder(const std::string& url) {
    auto decoder = std::make_unique<LibavMediaDecoder>();
    decoder->init(url);
    return decoder;
}

}  // namespace pivid
