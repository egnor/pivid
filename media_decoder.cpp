#include "media_decoder.h"

#include <drm_fourcc.h>
#include <sys/mman.h>

#include <cctype>
#include <map>
#include <system_error>

#include <fmt/core.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
}

#include "logging_policy.h"

namespace pivid {

namespace {

auto const& media_logger() {
    static const auto logger = make_logger("media");
    return logger;
}

//
// Libav-specific error handling and logging
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

void av_log_callback(void* avcl, int level, char const* format, va_list args) {
    static const auto logger = make_logger("libav");
    (void) avcl;

    std::string_view prefix;
    char buffer[8192];
    if (vsnprintf(buffer, sizeof(buffer), format, args) < 0) {
        logger->error("Bad libav log: {}\"{}\"", prefix, format);
    } else {
        std::string_view message{buffer};
        while (!message.empty() && isspace(message.back()))
            message.remove_suffix(1);
        switch (level) {
            case AV_LOG_PANIC: logger->critical("{}{}", prefix, message); break;
            case AV_LOG_FATAL: logger->error("{}{}", prefix, message); break;
            case AV_LOG_ERROR: logger->warn("{}{}", prefix, message); break;
            case AV_LOG_WARNING: logger->warn("{}{}", prefix, message); break;
            case AV_LOG_INFO: logger->debug("{}{}", prefix, message); break;
            case AV_LOG_VERBOSE: logger->debug("{}{}", prefix, message); break;
            case AV_LOG_DEBUG: logger->trace("{}{}", prefix, message); break;
            case AV_LOG_TRACE: logger->trace("{}{}", prefix, message); break;
            case AV_LOG_QUIET: logger->trace("{}{}", prefix, message); break;
            default: logger->error("?{}? {}{}", level, prefix, message); break;
        }
    }
}

//
// Memory buffer wrappers to AVFrame
//

class LibavDrmBuffer : public MemoryBuffer {
  public:
    ~LibavDrmBuffer() { if (map) munmap(map, av_obj->size); }
    virtual size_t size() const final { return av_obj->size; }
    virtual int dma_fd() const final { return av_obj->fd; }

    virtual uint8_t const* read() final {
        std::scoped_lock const lock{map_mutex};
        if (!map) {
            int const prot = PROT_READ, flags = MAP_SHARED;
            map = ::mmap(nullptr, av_obj->size, prot, flags, av_obj->fd, 0);
            if (map == MAP_FAILED) {
                map = nullptr;
                throw std::system_error(errno, std::system_category(), "mmap");
            }
        }
        return (uint8_t const*) map;
    }

    void init(std::shared_ptr<AVDRMFrameDescriptor const> av_drm, int index) {
        auto const* obj = &av_drm->objects[index];
        av_obj = std::shared_ptr<AVDRMObjectDescriptor const>{
            std::move(av_drm), obj
        };
    }

  private:
    std::shared_ptr<AVDRMObjectDescriptor const> av_obj;
    std::mutex map_mutex;
    void* map = nullptr;
};

class LibavPlainBuffer : public MemoryBuffer {
  public:
    virtual size_t size() const final {
        if (avf->format == AV_PIX_FMT_PAL8 && index == 1) return AVPALETTE_SIZE;
        return avf->linesize[index] * avf->height;
    }

    virtual uint8_t const* read() final { return avf->data[index]; }

    void init(std::shared_ptr<AVFrame> avf, int index) {
        ASSERT(index >= 0 && index < AV_NUM_DATA_POINTERS);
        ASSERT(avf->data[index]);
        this->index = index;
        this->avf = std::move(avf);
    }

  private:
    std::shared_ptr<AVFrame const> avf;
    int index = 0;
};

ImageBuffer image_from_av_drm(
    std::shared_ptr<AVDRMFrameDescriptor const> av_drm, XY<int> size
) {
    std::vector<std::shared_ptr<LibavDrmBuffer>> buffers;
    for (int oi = 0; oi < av_drm->nb_objects; ++oi) {
        buffers.push_back(std::make_shared<LibavDrmBuffer>());
        buffers.back()->init(av_drm, oi);
    }

    if (av_drm->nb_layers != 1) {
        throw std::runtime_error(fmt::format(
            "DRM frame has {} layers (expected 1)", av_drm->nb_layers
        ));
    }

    ImageBuffer image = {};
    image.size = size;

    switch (av_drm->layers[0].format) {
        case DRM_FORMAT_YUV420: image.fourcc = fourcc("I420"); break;
        case DRM_FORMAT_YUV422: image.fourcc = fourcc("Y42B"); break;
        default: image.fourcc = av_drm->layers[0].format;
    }

    for (int p = 0; p < av_drm->layers[0].nb_planes; ++p) {
        auto const& av_plane = av_drm->layers[0].planes[p];
        auto const& av_obj = av_drm->objects[av_plane.object_index];
        image.modifier = av_obj.format_modifier;
        image.channels.push_back({
            .memory = buffers[av_plane.object_index],
            .offset = av_plane.offset,
            .stride = av_plane.pitch,
        });
    }

    return image;
}

ImageBuffer image_from_av_plain(std::shared_ptr<AVFrame> av_frame) {
    ImageBuffer out = {};
    out.fourcc = avcodec_pix_fmt_to_codec_tag((AVPixelFormat) av_frame->format);
    out.size.x = av_frame->width;
    out.size.y = av_frame->height;

    for (int p = 0; p < AV_NUM_DATA_POINTERS; ++p) {
        if (!av_frame->data[p]) break;
        auto mem = std::make_shared<LibavPlainBuffer>();
        mem->init(av_frame, p);
        out.channels.push_back({
            .memory = std::move(mem),
            .offset = 0,
            .stride = av_frame->linesize[p],
        });
    }

    return out;
}

MediaFrame frame_from_av(std::shared_ptr<AVFrame> av, double time_base) {
    auto timestamp = av->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE) {
        timestamp = av->pts;
        if (timestamp == AV_NOPTS_VALUE) {
            timestamp = av->pkt_dts;
            if (timestamp == AV_NOPTS_VALUE)
                timestamp = 0;  // sigh
        }
    }

    MediaFrame out = {};
    out.time.begin = timestamp * time_base;
    out.time.end = (timestamp + av->pkt_duration) * time_base;
    if (out.time.end <= out.time.begin)
        out.time.end = std::nextafter(out.time.begin, out.time.begin + 1);

    out.is_corrupt = (av->flags & AV_FRAME_FLAG_CORRUPT);
    out.is_key_frame = av->key_frame;
    switch (av->pict_type) {
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

    if (av->format == AV_PIX_FMT_DRM_PRIME) {
        ASSERT(av->data[0] && !av->data[1]);
        XY<int> const size = {av->width, av->height};
        auto const* d = (AVDRMFrameDescriptor const*) av->data[0];
        std::shared_ptr<AVDRMFrameDescriptor const> sd{std::move(av), d};
        out.image = image_from_av_drm(std::move(sd), size);
    } else {
        out.image = image_from_av_plain(std::move(av));
    }
    return out;
}

//
// MediaDecoder implementation
//

class MediaDecoderDef : public MediaDecoder {
  public:
    virtual ~MediaDecoderDef() noexcept final {
        if (av_frame) av_frame_free(&av_frame);
        if (av_packet) av_packet_free(&av_packet);
        if (codec_context) avcodec_free_context(&codec_context);
        if (format_context) avformat_close_input(&format_context);
        if (!media_info.filename.empty())
            logger->debug("Closed \"{}\"", media_info.filename);
    }

    virtual MediaFileInfo const& file_info() const final { return media_info; }

    virtual void seek_before(double when) final {
        DEBUG(logger, "SEEK {:.3f}s \"{}\"", when, short_filename);
        ASSERT(format_context && codec_context);

        // Need to finish EOF flush, avcodec_flush_buffers() isn't enough?
        while (eof_sent_to_codec && !eof_seen_from_codec) {
            if (!av_frame) av_frame = check_alloc(av_frame_alloc());
            auto const err = avcodec_receive_frame(codec_context, av_frame);
            if (err == AVERROR_EOF) {
                DEBUG(logger, "  Finished EOF while seeking");
                eof_seen_from_codec = true;
            } else {
                check_av(err, "Decode frame", codec_context->codec->name);
                TRACE(logger, "  Flushed frame while seeking");
            }
        }

        if (av_packet) av_packet_unref(av_packet);
        if (av_frame) av_frame_unref(av_frame);
        avcodec_flush_buffers(codec_context);

        eof_sent_to_codec = false;
        eof_seen_from_codec = false;

        auto const tb = format_context->streams[stream_index]->time_base;
        int64_t const t = when / av_q2d(tb);
        check_av(
            avformat_seek_file(format_context, stream_index, 0, t, t, 0),
            "Seek file", media_info.filename
        );
        eof_seen_from_file = false;
    }

    virtual std::optional<MediaFrame> next_frame() final {
        if (eof_seen_from_codec) {
            TRACE(logger, "EOF reread \"{}\"", short_filename);
            return {};
        }

        DEBUG(logger, "READ \"{}\"", short_filename);
        ASSERT(format_context && codec_context);
        if (!av_packet) av_packet = check_alloc(av_packet_alloc());
        if (!av_frame) av_frame = check_alloc(av_frame_alloc());

        do {
            if (!av_frame->width) {
                auto const err = avcodec_receive_frame(codec_context, av_frame);
                if (err == AVERROR(EAGAIN) && !eof_sent_to_codec) {
                    TRACE(logger, "  (codec-to-app empty, more data needed)");
                } else if (err == AVERROR_EOF) {
                    DEBUG(logger, "  Got EOF from codec");
                    eof_seen_from_codec = true;
                    return {};
                } else {
                    check_av(err, "Decode frame", codec_context->codec->name);
                    TRACE(logger, "  Got frame from codec");
                }
            }

            if (!av_packet->data && !eof_seen_from_file) {
                auto const err = av_read_frame(format_context, av_packet);
                if (err == AVERROR_EOF) {
                    DEBUG(logger, "  Got EOF from file");
                    eof_seen_from_file = true;
                } else {
                    check_av(err, "Read", media_info.filename);
                    if (av_packet->stream_index == stream_index) {
                        TRACE(logger, 
                            "  Got packet from file ({})",
                            debug_size(av_packet->size)
                        );
                    } else {
                        av_packet_unref(av_packet);
                        ASSERT(av_packet->data == nullptr);
                        TRACE(logger, "  (ignoring nonvideo packet from file)");
                    }
                }
            }

            if (av_packet->data) {
                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err == AVERROR(EAGAIN)) {
                    TRACE(logger, "  (app-to-codec full, can't send data)");
                } else {
                    check_av(err, "Decode packet", codec_context->codec->name);
                    TRACE(
                        logger, "  Sent packet to codec ({})",
                        debug_size(av_packet->size)
                    );
                    av_packet_unref(av_packet);
                    ASSERT(av_packet->data == nullptr);
                }
            }

            if (!av_packet->data && eof_seen_from_file && !eof_sent_to_codec) {
                ASSERT(av_packet->data == nullptr);
                auto const err = avcodec_send_packet(codec_context, av_packet);
                if (err == AVERROR(EAGAIN)) {
                    TRACE(logger, "  (app-to-codec full, can't send EOF)");
                } else {
                    if (err != AVERROR_EOF)
                        check_av(err, "Decode EOF", codec_context->codec->name);
                    DEBUG(logger, "  Sent EOF to codec");
                    eof_sent_to_codec = true;
                }
            }

            // Stop when we got a frame *and* the codec won't accept writes
            // (always leave the codec with data to chew on if possible).
        } while (!(av_frame->width && (av_packet->data || eof_sent_to_codec)));

        auto const deleter = [](AVFrame* f) mutable {av_frame_free(&f);};
        std::shared_ptr<AVFrame> av_shared{this->av_frame, std::move(deleter)};
        av_frame = nullptr;  // The AVFrame is owned by av_shared now.

        auto const tb = format_context->streams[stream_index]->time_base;
        auto out = frame_from_av(std::move(av_shared), av_q2d(tb));
        DEBUG(logger, "  {}", debug(out));

        out.image.source_comment = fmt::format(
            "{} @{:.3f}", short_filename, out.time.begin
        );
        return out;
    }

    void init(std::string const& fn) {
        TRACE(logger, "Opening \"{}\"...", fn);
        media_info.filename = fn;
        auto const pos = fn.find_last_of('/');
        short_filename = (pos == std::string::npos) ? fn : fn.substr(pos + 1);

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
        ASSERT(codec_context);

        media_info.container_type = format_context->iformat->name;
        media_info.codec_name = codec_context->codec->name;
        media_info.pixel_format = av_get_pix_fmt_name(codec_context->pix_fmt);

        if (codec_context->width > 0 && codec_context->height > 0)
            media_info.size = {codec_context->width, codec_context->height};

        if (stream->duration > 0) {
            auto const tb = av_q2d(stream->time_base);
            media_info.duration = tb * stream->duration;
        } else if (format_context->duration > 0) {
            double constexpr tb = 1.0 / AV_TIME_BASE;
            media_info.duration = tb * format_context->duration;
        }

        if (stream->avg_frame_rate.num > 0)
            media_info.frame_rate = av_q2d(stream->avg_frame_rate);

        if (codec_context->bit_rate > 0) {
            media_info.bit_rate = codec_context->bit_rate;
        } else if (format_context->bit_rate > 0) {
            media_info.bit_rate = format_context->bit_rate;
        }

        logger->debug("{}", debug(media_info));
    }

  private:
    std::shared_ptr<log::logger> const logger = media_logger();
    AVFormatContext* format_context = nullptr;
    AVCodecContext* codec_context = nullptr;
    int stream_index = -1;
    MediaFileInfo media_info = {};
    std::string short_filename;

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
    av_log_set_callback(av_log_callback);
    auto decoder = std::make_unique<MediaDecoderDef>();
    decoder->init(filename);
    return decoder;
}

//
// Debugging utilities
//

std::vector<uint8_t> debug_tiff(ImageBuffer const& im) {
    av_log_set_callback(av_log_callback);
    media_logger()->trace("Encoding TIFF ({})...", debug(im));
    AVCodec const* tiff_codec = avcodec_find_encoder(AV_CODEC_ID_TIFF);
    if (!tiff_codec) throw std::runtime_error("No TIFF encoder found");

    std::map<uint32_t, AVPixelFormat> format_map;
    auto const* formats = tiff_codec->pix_fmts;
    for (int f = 0; formats[f] != AV_PIX_FMT_NONE; ++f) {
        auto const fourcc = avcodec_pix_fmt_to_codec_tag(formats[f]);
        if (fourcc) format_map[fourcc] = formats[f];
    }

    auto const format_iter = format_map.find(im.fourcc);
    if (format_iter == format_map.end()) {
        std::string text = "Bad pixel format for TIFF (";
        text += debug_fourcc(im.fourcc) + "), supported:";
        for (auto const& ff : format_map) text += " " + debug_fourcc(ff.first);
        throw std::invalid_argument(text);
    }

    std::shared_ptr<AVCodecContext> context{
        check_alloc(avcodec_alloc_context3(tiff_codec)),
        [](AVCodecContext* c) { avcodec_free_context(&c); }
    };
    context->width = im.size.x;
    context->height = im.size.y;
    context->time_base = {1, 30};  // Arbitrary but required.
    context->pix_fmt = format_iter->second;

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
    frame->width = im.size.x;
    frame->height = im.size.y;

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

    media_logger()->debug("  TIFF encoded ({})", debug_size(packet->size));
    return {packet->data, packet->data + packet->size};
}

std::string debug(MediaFileInfo const& i) {
    auto out = fmt::format(
        "\"{}\" {}:{}:{}",
        i.filename, i.container_type, i.codec_name, i.pixel_format
    );

    if (i.size) out += fmt::format(" {}x{}", i.size->x, i.size->y);
    if (i.frame_rate) out += fmt::format(" @{:.2f}fps", *i.frame_rate);
    if (i.duration) out += fmt::format(" {:.3f}s", *i.duration);
    if (i.bit_rate) out += fmt::format(" {:.3f}Mbps", *i.bit_rate * 1e-6);
    return out;
}

std::string debug(MediaFrame const& f) {
    auto out = debug(f.time);
    if (!f.frame_type.empty())
        out += fmt::format(" {:<2s}", f.frame_type);
    out += fmt::format(" {}", debug(f.image));
    if (f.is_key_frame) out += fmt::format(" KEY");
    if (f.is_corrupt) out += fmt::format(" CORRUPT");
    return out;
}

}  // namespace pivid
