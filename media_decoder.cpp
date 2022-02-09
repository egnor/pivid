#include "media_decoder.h"

#undef NDEBUG
#include <assert.h>
#include <drm_fourcc.h>
#include <sys/mman.h>

#include <system_error>

#include <fmt/core.h>
#include <fmt/chrono.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "logging_policy.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& media_logger() {
    static const auto logger = make_logger("media");
    return logger;
}

//
// Libav-specific error handling
//

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

int check_av(int avcode, std::string_view note, std::string_view detail) {
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
    virtual int dma_fd() const { return av_obj->fd; }

    virtual uint8_t const* read() {
        if (!mem) {
            int const prot = PROT_READ, flags = MAP_SHARED;
            void* r = ::mmap(nullptr, av_obj->size, prot, flags, av_obj->fd, 0);
            if (r == MAP_FAILED)
                throw std::system_error(errno, std::system_category(), "Mmap");
            mem = r;
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
};

class LibavPlainFrameMemory : public MemoryBuffer {
  public:
    virtual size_t size() const {
        if (avf->format == AV_PIX_FMT_PAL8 && index == 1) return AVPALETTE_SIZE;
        return avf->linesize[index] * avf->height;
    }

    virtual uint8_t const* read() { return avf->data[index]; }

    void init(std::shared_ptr<AVFrame> avf, int index) {
        assert(index >= 0 && index < AV_NUM_DATA_POINTERS);
        assert(avf->data[index]);
        this->index = index;
        this->avf = std::move(avf);
    }

  private:
    std::shared_ptr<AVFrame const> avf;
    int index = 0;
};

std::vector<ImageBuffer> images_from_av_drm(
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
        image.width = width;
        image.height = height;

        switch (av_layer.format) {
            case DRM_FORMAT_YUV420: image.fourcc = fourcc("I420"); break;
            case DRM_FORMAT_YUV422: image.fourcc = fourcc("Y42B"); break;
            default: image.fourcc = av_layer.format;
        }

        for (int p = 0; p < av_layer.nb_planes; ++p) {
            auto const& av_plane = av_layer.planes[p];
            auto const& av_obj = av_drm->objects[av_plane.object_index];
            image.modifier = av_obj.format_modifier;
            image.channels.push_back({
                .memory = buffers[av_plane.object_index],
                .offset = av_plane.offset,
                .stride = av_plane.pitch,
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
            .offset = 0,
            .stride = av_frame->linesize[p],
        });
    }

    return out;
}

MediaFrame frame_from_av(std::shared_ptr<AVFrame> frame, double time_base) {
    MediaFrame out = {};
    out.time = std::chrono::duration<double>(frame->pts * time_base);
    out.is_corrupt = (frame->flags & AV_FRAME_FLAG_CORRUPT);
    out.is_key_frame = frame->key_frame;
    switch (frame->pict_type) {
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

    if (frame->format == AV_PIX_FMT_DRM_PRIME) {
        assert(frame->data[0] && !frame->data[1]);
        int const width = frame->width, height = frame->height;
        auto const* d = (AVDRMFrameDescriptor const*) frame->data[0];
        std::shared_ptr<AVDRMFrameDescriptor const> sd{std::move(frame), d};
        out.images = images_from_av_drm(std::move(sd), width, height);
    } else {
        out.images.push_back(image_from_av_plain(std::move(frame)));
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

    virtual std::optional<MediaFrame> next_frame() {
        assert(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc());
        if (!av_frame) av_frame = check_alloc(av_frame_alloc());

        do {
            if (eof_seen_from_codec) {
                logger->trace("> (EOF reached, no more frames)");
                return {};
            }

            if (!av_frame->width) {
                logger->trace("Checking for frame from codec...");
                auto const err = avcodec_receive_frame(codec_context, av_frame);
                if (err == AVERROR(EAGAIN)) {
                    logger->trace("> (codec needs more data, can't get frame)");
                } else if (err == AVERROR_EOF) {
                    logger->info("Got EOF from codec");
                    eof_seen_from_codec = true;
                } else {
                    check_av(err, "Decode frame", codec_context->codec->name);
                    logger->trace("> Got frame from codec");
                }
            }

            if (!av_packet->data && !eof_seen_from_file) {
                logger->trace("Reading from file...");
                auto const err = av_read_frame(format_context, av_packet);
                if (err == AVERROR_EOF) {
                    logger->debug("> Got EOF from file");
                    eof_seen_from_file = true;
                } else {
                    check_av(err, "Read", format_context->iformat->name);
                    if (av_packet->stream_index == stream_index) {
                        logger->trace(
                            "> Read packet from file ({})",
                            debug_size(av_packet->size)
                        );
                    } else {
                        av_packet_unref(av_packet);
                        assert(av_packet->data == nullptr);
                        logger->trace("> (got other stream packet, ignoring)");
                    }
                }
            }

            if (av_packet->data) {
                logger->trace(
                    "Sending packet to codec ({})...",
                    debug_size(av_packet->size)
                );
                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err == AVERROR(EAGAIN)) {
                    logger->trace("> (app-to-codec full, can't send data)");
                } else {
                    check_av(err, "Decode packet", codec_context->codec->name);
                    logger->trace("> Sent packet to codec");
                    av_packet_unref(av_packet);
                    assert(av_packet->data == nullptr);
                }
            }

            if (eof_seen_from_file && !eof_sent_to_codec) {
                assert(av_packet->data == nullptr);
                logger->trace("Sending EOF to codec...");
                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err == AVERROR(EAGAIN)) {
                    logger->trace("> (app-to-codec full, can't send EOF)");
                } else {
                    if (err != AVERROR_EOF)
                        check_av(err, "Decode EOF", codec_context->codec->name);
                    logger->debug("Sent EOF to codec");
                    eof_sent_to_codec = true;
                }
            }

            // Stop when we got a frame *and* the codec won't accept writes
            // (always leave the codec with data to chew on if possible).
        } while (!(av_frame->width && (av_packet->data || eof_sent_to_codec)));

        auto const deleter = [](AVFrame* f) mutable {av_frame_free(&f);};
        std::shared_ptr<AVFrame> av_shared{this->av_frame, std::move(deleter)};
        av_frame = nullptr;  // Owned by shared_ptr now.

        logger->trace("Converting frame...");
        auto const* stream = format_context->streams[stream_index];
        auto f = frame_from_av(std::move(av_shared), av_q2d(stream->time_base));
        if (logger->should_log(log_level::debug))
            logger->debug("{}", debug(f));

        return f;
    }

    void init(std::string const& fn) {
        logger->trace("Opening media ({})...", fn);
        media_info.filename = fn;

        check_av(
            avformat_open_input(&format_context, fn.c_str(), nullptr, nullptr),
            "Open media", fn
        );

        check_av(
            avformat_find_stream_info(format_context, nullptr),
            "Find stream info", fn
        );

        AVCodec* default_codec = nullptr;
        stream_index = check_av(
            av_find_best_stream(
                format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &default_codec, 0
            ), "Find video stream", fn
        );

        if (default_codec == nullptr || stream_index < 0)
            check_av(AVERROR_DECODER_NOT_FOUND, "Find codec", fn);

        AVCodec* preferred_codec = nullptr;
        if (default_codec->id == AV_CODEC_ID_H264)
            preferred_codec = avcodec_find_decoder_by_name("h264_v4l2m2m");

        int open_err = AVERROR_DECODER_NOT_FOUND;
        auto const* stream = format_context->streams[stream_index];
        for (auto* try_codec : {preferred_codec, default_codec}) {
            if (!try_codec) continue;
            codec_context = check_alloc(avcodec_alloc_context3(try_codec));
            check_av(
                avcodec_parameters_to_context(codec_context, stream->codecpar),
                "Codec parameters", try_codec->name
            );

            codec_context->thread_count = 4;
            codec_context->get_format = pixel_format_callback;
            open_err = avcodec_open2(codec_context, try_codec, nullptr);
            if (open_err >= 0) break;

            avcodec_free_context(&codec_context);
        }

        check_av(open_err, "Open codec", default_codec->name);
        assert(codec_context);

        double const time_base = av_q2d(stream->time_base);
        media_info.container_type = format_context->iformat->name;
        media_info.codec_name = codec_context->codec->name;
        media_info.pixel_format = av_get_pix_fmt_name(codec_context->pix_fmt);

        if (codec_context->width > 0 && codec_context->height > 0) {
            media_info.width = codec_context->width;
            media_info.height = codec_context->height;
        }

        double seconds = 0;
        if (stream->duration > 0) {
            seconds = stream->duration * time_base;
        } else if (format_context->duration > 0) {
            seconds = format_context->duration * 1.0 / AV_TIME_BASE;
        }
        media_info.duration = std::chrono::duration<double>(seconds);

        if (stream->avg_frame_rate.num > 0)
            media_info.frame_rate = av_q2d(stream->avg_frame_rate);

        if (codec_context->bit_rate > 0) {
            media_info.bit_rate = codec_context->bit_rate;
        } else if (format_context->bit_rate > 0) {
            media_info.bit_rate = format_context->bit_rate;
        }

        logger->info("Opened: {}", debug(media_info));
    }

  private:
    std::shared_ptr<log::logger> const logger = media_logger();
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

std::unique_ptr<MediaDecoder> open_media_decoder(const std::string& filename) {
    auto decoder = std::make_unique<LibavMediaDecoder>();
    decoder->init(filename);
    return decoder;
}

std::vector<uint8_t> debug_tiff(ImageBuffer const& im) {
    media_logger()->trace("Encoding TIFF ({})...", debug(im));
    AVCodec const* tiff_codec = avcodec_find_encoder(AV_CODEC_ID_TIFF);
    if (!tiff_codec) throw std::runtime_error("No TIFF encoder found");

    std::shared_ptr<AVCodecContext> context{
        check_alloc(avcodec_alloc_context3(tiff_codec)),
        [](AVCodecContext* c) { avcodec_free_context(&c); }
    };
    context->width = im.width;
    context->height = im.height;
    context->time_base = {1, 30};  // Arbitrary but required.
    context->pix_fmt = AV_PIX_FMT_NONE;

    auto const* formats = tiff_codec->pix_fmts;
    for (int f = 0; context->pix_fmt == AV_PIX_FMT_NONE; ++f) {
        if (formats[f] == AV_PIX_FMT_NONE) {
            throw std::invalid_argument(fmt::format(
                "Bad pixel format for TIFF ({})", debug_fourcc(im.fourcc)
            ));
        }
        if (avcodec_pix_fmt_to_codec_tag(formats[f]) == im.fourcc)
            context->pix_fmt = formats[f];
    }

    check_av(
        av_opt_set(context->priv_data, "compression_algo", "deflate", 0),
        "TIFF compression", "deflate"
    );

    check_av(
        avcodec_open2(context.get(), tiff_codec, nullptr),
        "Encoding context", context->codec->name
    );

    std::shared_ptr<AVFrame> frame{
        check_alloc(av_frame_alloc()), [](AVFrame* f) { av_frame_free(&f); }
    };
    frame->format = context->pix_fmt;
    frame->width = im.width;
    frame->height = im.height;

    if (im.channels.size() > AV_NUM_DATA_POINTERS)
        throw std::length_error("Too many image channels to encode");
    for (size_t p = 0; p < im.channels.size(); ++p) {
        auto const& chan = im.channels[p];
        frame->data[p] = ((uint8_t*) chan.memory->read()) + chan.offset;
        frame->linesize[p] = chan.stride;
    }

    check_av(
        avcodec_send_frame(context.get(), frame.get()),
        "Encoding frame", context->codec->name
    );

    std::shared_ptr<AVPacket> packet{
        check_alloc(av_packet_alloc()), [](AVPacket* p) { av_packet_free(&p); }
    };

    check_av(
        avcodec_receive_packet(context.get(), packet.get()),
        "Encoding packet", context->codec->name
    );

    media_logger()->debug("> TIFF encoded ({})", debug_size(packet->size));
    return {packet->data, packet->data + packet->size};
}

std::string debug(MediaInfo const& i) {
    auto out = fmt::format(
        "\"{}\" {}:{}:{}",
        i.filename, i.container_type, i.codec_name, i.pixel_format
    );

    if (i.width && i.height) out += fmt::format(" {}x{}", *i.width, *i.height);
    if (i.frame_rate) out += fmt::format(" @{:.2f}fps", *i.frame_rate);
    if (i.duration) out += fmt::format(" {:.1}", *i.duration);
    if (i.bit_rate) out += fmt::format(" {:.3f}Mbps", *i.bit_rate * 1e-6);
    return out;
}

std::string debug(MediaFrame const& f) {
    auto out = fmt::format("{:5.3}", f.time);
    if (!f.frame_type.empty())
        out += fmt::format(" {:<2s}", f.frame_type);
    for (size_t l = 0; l < f.images.size(); ++l)
        out += fmt::format(" {}{}", l ? "+ " : "", debug(f.images[l]));
    if (f.is_key_frame) out += fmt::format(" KEY");
    if (f.is_corrupt) out += fmt::format(" CORRUPT");
    return out;
}

}  // namespace pivid
