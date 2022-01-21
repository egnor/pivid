#include "display_output.h"

#undef NDEBUG
#include <assert.h>
#include <drm/drm.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <type_traits>

#include <fmt/core.h>

#include "logging_policy.h"
#include "unix_system.h"

namespace pivid {

namespace {

std::shared_ptr<spdlog::logger> const& display_logger() {
    static const auto logger = make_logger("display");
    return logger;
}

//
// Helpers for KMS/DRM interface
//

// Support KMS/DRM ioctl conventions for variable size arrays;
// returns true if the ioctl needs to be re-submitted with a resized array.
template <typename Pointer, typename Count, typename Item>
bool size_vec(Pointer* ptr, Count* count, std::vector<Item>* v) {
    if (*count == v->size() && *ptr == (Pointer) v->data()) return false;
    v->resize(*count);
    *ptr = (Pointer) v->data();
    return true;
}

DisplayMode mode_from_drm(drm_mode_modeinfo const& drm) {
    auto const sign = [&drm](uint32_t nflag, uint32_t pflag) -> int8_t {
        return (drm.flags & nflag) ? -1 : (drm.flags & pflag) ? +1 : 0;
    };

    return {
        .name = drm.name,
        .horiz = {
            .display = drm.hdisplay,
            .sync_start = drm.hsync_start,
            .sync_end = drm.hsync_end,
            .total = drm.htotal,
            .doubling = sign(DRM_MODE_FLAG_CLKDIV2, DRM_MODE_FLAG_DBLCLK),
            .sync_polarity = sign(DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PHSYNC),
        },
        .vert = {
            .display = drm.vdisplay,
            .sync_start = drm.vsync_start,
            .sync_end = drm.vsync_end,
            .total = drm.vtotal,
            .doubling = sign(DRM_MODE_FLAG_INTERLACE, DRM_MODE_FLAG_DBLSCAN),
            .sync_polarity = sign(DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_PVSYNC),
        },
        .pixel_khz = int(drm.clock), 
        .refresh_hz = int(drm.vrefresh), 
    };
}

drm_mode_modeinfo mode_to_drm(DisplayMode const& mode) {
    drm_mode_modeinfo out = {
        .clock = uint32_t(mode.pixel_khz),
        .hdisplay = uint16_t(mode.horiz.display),
        .hsync_start = uint16_t(mode.horiz.sync_start),
        .hsync_end = uint16_t(mode.horiz.sync_end),
        .htotal = uint16_t(mode.horiz.total),
        .hskew = 0,
        .vdisplay = uint16_t(mode.vert.display),
        .vsync_start = uint16_t(mode.vert.sync_start),
        .vsync_end = uint16_t(mode.vert.sync_end),
        .vtotal = uint16_t(mode.vert.total),
        .vscan = uint16_t(mode.vert.doubling ? 2 : 1),
        .vrefresh = uint32_t(mode.refresh_hz),
        .flags = uint32_t(
            (mode.horiz.sync_polarity > 0 ? DRM_MODE_FLAG_PHSYNC : 0) |
            (mode.horiz.sync_polarity < 0 ? DRM_MODE_FLAG_NHSYNC : 0) |
            (mode.vert.sync_polarity > 0 ? DRM_MODE_FLAG_PVSYNC : 0) |
            (mode.vert.sync_polarity < 0 ? DRM_MODE_FLAG_NVSYNC : 0) |
            (mode.vert.doubling < 0 ? DRM_MODE_FLAG_INTERLACE : 0) |
            (mode.horiz.doubling > 0 ? DRM_MODE_FLAG_DBLCLK : 0) |
            (mode.horiz.doubling < 0 ? DRM_MODE_FLAG_CLKDIV2 : 0)
        ),
        .type = uint32_t(DRM_MODE_TYPE_USERDEF),
        .name = {},
    };

    strncpy(out.name, mode.name.c_str(), sizeof(out.name) - 1);  // NUL term.
    return out;
}

uint32_t format_to_drm(uint32_t format) {
    // Note, ffmpeg/AVI/"standard" fourcc is big endian, DRM is little endian
    switch (format) {
      case fourcc("0BGR"): return DRM_FORMAT_RGBX8888;
      case fourcc("0RGB"): return DRM_FORMAT_BGRX8888;
      case fourcc("ABGR"): return DRM_FORMAT_RGBA8888;
      case fourcc("ARGB"): return DRM_FORMAT_BGRA8888;
      case fourcc("BGR0"): return DRM_FORMAT_XRGB8888;
      case fourcc("BGRA"): return DRM_FORMAT_ARGB8888;
      case fourcc("BGR\x10"): return DRM_FORMAT_BGR565;
      case fourcc("BGR\x18"): return DRM_FORMAT_RGB888;
      case fourcc("I420"): return DRM_FORMAT_YUV420;
      case fourcc("NV12"): return DRM_FORMAT_NV12;
      case fourcc("NV21"): return DRM_FORMAT_NV21;
      case fourcc("PAL\x08"): return DRM_FORMAT_C8;
      case fourcc("RGB0"): return DRM_FORMAT_XBGR8888;
      case fourcc("RGBA"): return DRM_FORMAT_ABGR8888;
      case fourcc("RGB\x10"): return DRM_FORMAT_RGB565;
      case fourcc("RGB\x18"): return DRM_FORMAT_BGR888;
      case fourcc("Y42B"): return DRM_FORMAT_YUV422;
      default: return format;  // Might match!
    }
}

void to_premultiplied_rgba(
    uint32_t format, int width, uint8_t const* from, uint8_t* to
) {
    switch (format) {
      case fourcc("ABGR"):
        for (int x = 0; x < width; ++x) {
            uint8_t const alpha = to[3 + 4 * x] = from[4 * x];
            to[0 + 4 * x] = from[3 + 4 * x] * alpha / 255;
            to[1 + 4 * x] = from[2 + 4 * x] * alpha / 255;
            to[2 + 4 * x] = from[1 + 4 * x] * alpha / 255;
        }
        break;

      case fourcc("ARGB"):
        for (int x = 0; x < width; ++x) {
            uint8_t const alpha = to[3 + 4 * x] = from[4 * x];
            to[0 + 4 * x] = from[1 + 4 * x] * alpha / 255;
            to[1 + 4 * x] = from[2 + 4 * x] * alpha / 255;
            to[2 + 4 * x] = from[3 + 4 * x] * alpha / 255;
        }
        break;

      case fourcc("RGBA"):
        for (int x = 0; x < width; ++x) {
            uint8_t const alpha = to[3 + 4 * x] = from[3 + 4 * x];
            to[0 + 4 * x] = from[0 + 4 * x] * alpha / 255;
            to[1 + 4 * x] = from[1 + 4 * x] * alpha / 255;
            to[2 + 4 * x] = from[2 + 4 * x] * alpha / 255;
        }
        break;

      case fourcc("BGRA"):
        for (int x = 0; x < width; ++x) {
            uint8_t const alpha = to[3 + 4 * x] = from[3 + 4 * x];
            to[0 + 4 * x] = from[2 + 4 * x] * alpha / 255;
            to[1 + 4 * x] = from[1 + 4 * x] * alpha / 255;
            to[2 + 4 * x] = from[0 + 4 * x] * alpha / 255;
        }
        break;
    }
}

class DrmDumbBuffer : public MemoryBuffer {
  public:
    DrmDumbBuffer(std::shared_ptr<FileDescriptor> fd, int w, int h, int bpp) {
        ddat.height = h;
        ddat.width = w;
        ddat.bpp = bpp;
        fd->ioc<DRM_IOCTL_MODE_CREATE_DUMB>(&ddat).ex("DRM buffer");
        this->fd = std::move(fd);
    }

    virtual ~DrmDumbBuffer() {
        if (!ddat.handle) return;
        drm_mode_destroy_dumb dddat = {.handle = ddat.handle};
        (void) fd->ioc<DRM_IOCTL_MODE_DESTROY_DUMB>(&dddat);
    }

    virtual size_t size() const { return ddat.size; }

    virtual uint8_t const* read() {
        if (!mem) {
            drm_mode_map_dumb mdat = {};
            mdat.handle = ddat.handle;
            fd->ioc<DRM_IOCTL_MODE_MAP_DUMB>(&mdat).ex("Map DRM buffer");
            mem = fd->mmap(
                ddat.size, PROT_READ | PROT_WRITE, MAP_SHARED, mdat.offset
            ).ex("Memory map DRM buffer");
        }
        return (uint8_t const*) mem.get();
    }

    virtual uint32_t drm_handle() const { return ddat.handle; }
    uint8_t* write() { read(); return (uint8_t*) mem.get(); }
    ptrdiff_t stride() const { return ddat.pitch; }

  private:
    std::shared_ptr<FileDescriptor> fd;
    drm_mode_create_dumb ddat = {};
    std::shared_ptr<void> mem;

    DrmDumbBuffer(DrmDumbBuffer const&) = delete;
    DrmDumbBuffer& operator=(DrmDumbBuffer const&) = delete;
};

class DrmBufferImport {
  public:
    DrmBufferImport(
        std::shared_ptr<FileDescriptor> fd, MemoryBuffer const& buffer
    ) {
        hdat.fd = buffer.dma_fd();
        fd->ioc<DRM_IOCTL_PRIME_FD_TO_HANDLE>(&hdat).ex("Import DMA");
        this->fd = std::move(fd);
    }

    ~DrmBufferImport() {
        if (!hdat.handle) return;
        drm_gem_close cdat = {.handle = hdat.handle, .pad = 0};
        (void) fd->ioc<DRM_IOCTL_GEM_CLOSE>(cdat);
    }

    uint32_t drm_handle() const { return hdat.handle; }

  private:
    std::shared_ptr<FileDescriptor> fd;
    drm_prime_handle hdat = {};

    DrmBufferImport(DrmBufferImport const&) = delete;
    DrmBufferImport& operator=(DrmBufferImport const&) = delete;
};

class DrmFrameBuffer {
  public:
    DrmFrameBuffer(std::shared_ptr<FileDescriptor> fd, ImageBuffer const& im) {
        fdat.width = im.width;
        fdat.height = im.height;
        fdat.pixel_format = format_to_drm(im.fourcc);
        fdat.flags = DRM_MODE_FB_MODIFIERS;

        size_t const max_channels = std::extent_v<decltype(fdat.handles)>;
        if (im.channels.size() > max_channels)
            throw std::length_error("Too many image channels for DRM");

        std::vector<std::unique_ptr<DrmBufferImport>> imports;
        for (size_t ci = 0; ci < im.channels.size(); ++ci) {
            auto const& channel = im.channels[ci];
            fdat.pitches[ci] = channel.stride;
            fdat.offsets[ci] = channel.offset;
            fdat.modifier[ci] = im.modifier;
            fdat.handles[ci] = channel.memory->drm_handle();

            for (size_t pci = 0; pci < ci && !fdat.handles[ci]; ++pci) {
                if (channel.memory == im.channels[pci].memory)
                    fdat.handles[ci] = fdat.handles[pci];
            }

            if (!fdat.handles[ci]) {
                auto const& mem = *channel.memory;
                imports.push_back(std::make_unique<DrmBufferImport>(fd, mem));
                fdat.handles[ci] = imports.back()->drm_handle();
            }
        }

        log->trace("Creating DRM framebuffer...");
        this->fd = std::move(fd);
        this->fd->ioc<DRM_IOCTL_MODE_ADDFB2>(&fdat).ex("DRM framebuffer");
        if (log->should_log(spdlog::level::level_enum::debug))
            log->debug("Loaded fb={} {}", fdat.fb_id, debug(im));
    }

    ~DrmFrameBuffer() {
        if (!fdat.fb_id) return;
        log->trace("Removing DRM framebuffer...");
        (void) fd->ioc<DRM_IOCTL_MODE_RMFB>(&fdat.fb_id);
        log->debug("Unloaded fb={} {}x{}", fdat.fb_id, fdat.width, fdat.height);
    }

    uint32_t const* id() const { return &fdat.fb_id; }

  private:
    std::shared_ptr<spdlog::logger> log = display_logger();
    std::shared_ptr<FileDescriptor> fd;
    drm_mode_fb_cmd2 fdat = {};

    DrmFrameBuffer(DrmFrameBuffer const&) = delete;
    DrmFrameBuffer& operator=(DrmFrameBuffer const&) = delete;
};

//
// DisplayDriver implementation
//

class DrmDriver : public DisplayDriver {
  public:
    DrmDriver() {}

    virtual std::vector<DisplayConnectorStatus> scan_connectors() {
        log->trace("Scanning connectors...");
        std::vector<DisplayConnectorStatus> out;
        for (auto const& id_conn : connectors) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                cdat.count_props = cdat.count_encoders = 0;
                fd->ioc<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat).ex("DRM connector");
            } while (size_vec(&cdat.modes_ptr, &cdat.count_modes, &modes));

            DisplayConnectorStatus status = {};
            status.id = id_conn.first;
            status.name = id_conn.second.name;
            status.display_detected = (cdat.connection == 1);

            for (auto const& mode : modes) {
                if (!(mode.flags & DRM_MODE_FLAG_3D_MASK))
                    status.display_modes.push_back(mode_from_drm(mode));
            }

            if (cdat.encoder_id) {
                drm_mode_get_encoder edat = {};
                edat.encoder_id = cdat.encoder_id;
                fd->ioc<DRM_IOCTL_MODE_GETENCODER>(&edat).ex("DRM encoder");
                if (edat.crtc_id) {
                    auto const& drm_mode = crtcs.at(edat.crtc_id).active.mode;
                    status.active_mode = mode_from_drm(drm_mode);
                }
            }

            out.push_back(std::move(status));
        }
        log->debug("Found {} display connectors", out.size());
        return out;
    }

    virtual std::shared_ptr<uint32_t const> load_image(ImageBuffer im) {
        log->trace("Loading {}x{} image...", im.width, im.height);
        switch (im.fourcc) {
          case fourcc("ABGR"):
          case fourcc("ARGB"):
          case fourcc("BGRA"):
          case fourcc("RGBA"):
            if (im.channels.size() != 1)
                throw std::invalid_argument("Bad channel count for RGBA image");

            // Assume a DRM handle means it's been converted.
            if (!im.channels[0].memory->drm_handle()) {
                log->trace("> (copying and converting RGBA...)");
                auto buf = std::make_shared<DrmDumbBuffer>(
                    fd, im.width, im.height, 32
                );

                auto const& chan = im.channels[0];
                for (int y = 0; y < im.height; ++y) {
                    to_premultiplied_rgba(
                        im.fourcc, im.width,
                        chan.memory->read() + y * chan.stride + chan.offset,
                        buf->write() + y * buf->stride()
                    );
                }

                im.fourcc = fourcc("RGBA");
                im.channels[0].offset = 0;
                im.channels[0].stride = buf->stride();
                im.channels[0].memory = std::move(buf);
            }
        }

        for (size_t ci = 0; ci < im.channels.size(); ++ci) { 
            auto *chan = &im.channels[ci];
            if (chan->memory->dma_fd() >= 0 || chan->memory->drm_handle())
                continue;

            log->trace("> (copying {} ch{}...)", debug_fourcc(im.fourcc), ci);
            auto buf = std::make_shared<DrmDumbBuffer>(
                fd, im.width, im.height, 8 * chan->stride / im.width
            );

            for (int y = 0; y < im.height; ++y) {
                memcpy(
                    buf->write() + y * buf->stride(),
                    chan->memory->read() + y * chan->stride + chan->offset,
                    std::min(buf->stride(), chan->stride)
                );
            }

            chan->offset = 0;
            chan->stride = buf->stride();
            chan->memory = std::move(buf);
        }

        auto fb = std::make_shared<DrmFrameBuffer>(fd, std::move(im));
        return std::shared_ptr<const uint32_t>(fb, fb->id());
    }

    virtual void request_update(DisplayRequest const& request) {
        auto* const conn = &connectors.at(request.connector_id);
        log->trace("Starting {} update...", conn->name);

        process_events();
        auto* crtc = conn->using_crtc;
        if (crtc && crtc->pending_flip)
            throw std::invalid_argument("Update request before previous done");

        if (!crtc && !request.mode.refresh_hz) {
            log->trace("> (connector remains disabled, no change)");
            return;
        }

        if (!crtc) {
            for (auto* const c : conn->usable_crtcs) {
                if (c->used_by_conn) continue;
                assert(!c->pending_flip);
                crtc = c;
                break;
            }
            if (!crtc) throw std::runtime_error("No DRM CRTC: " + conn->name);
        }

        // Build the atomic update and the state that will result.
        std::map<uint32_t, std::map<PropId const*, uint64_t>> props;
        std::shared_ptr<uint32_t const> mode_blob;
        Crtc::State next = {};
        int32_t writeback_fence_fd = -1;

        if (!request.mode.refresh_hz) {
            log->debug("{} :: [disabling]", conn->name);
            props[conn->id][&conn->CRTC_ID] = 0;
            props[crtc->id][&crtc->ACTIVE] = 0;
            // Leave next state zeroed.
        } else {
            next.mode = mode_to_drm(request.mode);
            static_assert(sizeof(crtc->active.mode) == sizeof(next.mode));
            if (memcmp(&crtc->active.mode, &next.mode, sizeof(next.mode))) {
                if (log->should_log(spdlog::level::level_enum::debug))
                    log->debug("{} :: {}", conn->name, debug(request.mode));
                mode_blob = create_blob(next.mode);
                props[crtc->id][&crtc->MODE_ID] = *mode_blob;
            }

            if (conn->WRITEBACK_FB_ID.prop_id) {
                int const w = next.mode.hdisplay, h = next.mode.vdisplay;
                log->trace("Making {}x{} writeback image...", w, h);
                auto buf = std::make_shared<DrmDumbBuffer>(fd, w, h, 32);

                Writeback wb = {};
                wb.image.fourcc = fourcc("RGBA");
                wb.image.width = w;
                wb.image.height = h;
                wb.image.channels.resize(1);
                wb.image.channels[0].stride = buf->stride();
                wb.image.channels[0].memory = std::move(buf);
                wb.fb_id = load_image(wb.image);

                int const id = *wb.fb_id;
                log->debug("{} >> fb={} {}", conn->name, id, debug(wb.image));
                next.writeback = std::move(wb);
                props[conn->id][&conn->WRITEBACK_FB_ID] = id;
                props[conn->id][&conn->WRITEBACK_OUT_FENCE_PTR] =
                    (uint64_t) &writeback_fence_fd;
            }

            if (!conn->using_crtc || next.writeback) {
                props[conn->id][&conn->CRTC_ID] = crtc->id;
                props[crtc->id][&crtc->ACTIVE] = 1;
            }

            auto plane_iter = crtc->usable_planes.begin();
            for (size_t li = 0; li < request.layers.size(); ++li) {
                // Find an appropriate plane (Primary=1, Overlay=0)
                auto const& layer = request.layers[li];
                uint64_t const wanted_type = li ? 0 : 1;
                for (;; ++plane_iter) {
                    if (plane_iter == crtc->usable_planes.end())
                        throw std::runtime_error("No DRM plane: " + conn->name);

                    if ((*plane_iter)->type.init_value != wanted_type) continue;
                    if ((*plane_iter)->used_by_crtc == crtc) break;
                    if (!(*plane_iter)->used_by_crtc) break;
                }

                auto* plane = *plane_iter++;
                int fb_id = *layer.loaded_image;
                next.using_planes.push_back(plane);
                next.images.push_back(std::move(layer.loaded_image));

                if (log->should_log(spdlog::level::level_enum::debug)) {
                    log->debug(
                        "{} << plane={} fb={}", conn->name, plane->id, fb_id
                    );
                }

                auto* plane_props = &props[plane->id];
                (*plane_props)[&plane->CRTC_ID] = crtc->id;
                (*plane_props)[&plane->FB_ID] = fb_id;

                auto fix = [](double d) -> int64_t { return d * 65536.0; };
                (*plane_props)[&plane->SRC_X] = fix(layer.source_x);
                (*plane_props)[&plane->SRC_Y] = fix(layer.source_y);
                (*plane_props)[&plane->SRC_W] = fix(layer.source_width);
                (*plane_props)[&plane->SRC_H] = fix(layer.source_height);
                (*plane_props)[&plane->CRTC_X] = layer.screen_x;
                (*plane_props)[&plane->CRTC_Y] = layer.screen_y;
                (*plane_props)[&plane->CRTC_W] = layer.screen_width;
                (*plane_props)[&plane->CRTC_H] = layer.screen_height;
            }
        }

        std::vector<uint32_t> obj_ids;
        std::vector<uint32_t> obj_prop_counts;
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> prop_values;
        for (auto const& obj_prop : props) {
            obj_ids.push_back(obj_prop.first);
            obj_prop_counts.push_back(obj_prop.second.size());
            for (auto const& prop_value : obj_prop.second) {
               prop_ids.push_back(prop_value.first->prop_id);
               prop_values.push_back(prop_value.second);
            }
        }

        drm_mode_atomic atomic = {
            .flags =
                DRM_MODE_PAGE_FLIP_EVENT |
                DRM_MODE_ATOMIC_NONBLOCK |
                DRM_MODE_ATOMIC_ALLOW_MODESET,
            .count_objs = (uint32_t) obj_ids.size(),
            .objs_ptr = (uint64_t) obj_ids.data(),
            .count_props_ptr = (uint64_t) obj_prop_counts.data(),
            .props_ptr = (uint64_t) prop_ids.data(),
            .prop_values_ptr = (uint64_t) prop_values.data(),
            .reserved = 0,
            .user_data = 0,
        };

        log->trace("Committing DRM atomic update...");
        auto ret = fd->ioc<DRM_IOCTL_MODE_ATOMIC>(&atomic);
        if (ret.err == EBUSY) {
            log->trace("> (driver busy, retrying update without NONBLOCK)");
            atomic.flags &= ~DRM_MODE_ATOMIC_NONBLOCK;
            ret = fd->ioc<DRM_IOCTL_MODE_ATOMIC>(&atomic);
        }

        if (writeback_fence_fd >= 0) {
            log->trace("> (writeback fence fd={})", writeback_fence_fd);
            if (!next.writeback) next.writeback.emplace();
            next.writeback->fence = sys->adopt(writeback_fence_fd);
        }

        ret.ex("DRM atomic update");
        log->debug("{} update committed", conn->name);
        crtc->pending_flip.emplace(std::move(next));
        conn->using_crtc = crtc;
        crtc->used_by_conn = conn;
    }

    virtual std::optional<DisplayRequestDone> is_request_done(uint32_t id) {
        auto* const conn = &connectors.at(id);
        log->trace("Checking {} completion...", conn->name);
        process_events();

        if (!conn->using_crtc) {
            log->trace("> (connector not active)");
            return DisplayRequestDone{};
        }

        if (conn->using_crtc->pending_flip) {
            log->trace("> (previous update incomplete)");
            return {};
        }

        return DisplayRequestDone{};
    }

    void open(std::shared_ptr<UnixSystem> sys, std::string const& dev) {
        this->sys = std::move(sys);
        fd = this->sys->open(dev.c_str(), O_RDWR | O_NONBLOCK).ex(dev);
        fd->ioc<DRM_IOCTL_SET_MASTER>().ex("Become DRM master");
        fd->ioc<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_ATOMIC, 1}
        ).ex("Enable DRM atomic modesetting");
        fd->ioc<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1}
        ).ex("Enable DRM universal planes");
        fd->ioc<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1}
        ).ex("Enable DRM universal planes");

        drm_mode_card_res res = {};
        std::vector<uint32_t> crtc_ids;
        std::vector<uint32_t> conn_ids;
        do {
            res.count_fbs = res.count_encoders = 0;  // Don't use these.
            fd->ioc<DRM_IOCTL_MODE_GETRESOURCES>(&res).ex("DRM resources");
        } while (
            size_vec(&res.crtc_id_ptr, &res.count_crtcs, &crtc_ids) +
            size_vec(&res.connector_id_ptr, &res.count_connectors, &conn_ids)
        );

        for (auto const crtc_id : crtc_ids) {
            drm_mode_crtc ccdat = {};
            ccdat.crtc_id = crtc_id;
            fd->ioc<DRM_IOCTL_MODE_GETCRTC>(&ccdat).ex("DRM CRTC");

            auto* crtc = &crtcs[crtc_id];
            crtc->id = crtc_id;
            require_prop_ids(crtc_id, &crtc->prop_ids);
            if (ccdat.mode_valid)
                crtc->active.mode = ccdat.mode;
        }

        for (auto const conn_id : conn_ids) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = conn_id;
            std::vector<uint32_t> enc_ids;
            do {
                cdat.count_props = cdat.count_modes = 0;
                fd->ioc<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat).ex("DRM conn");
            } while (
                size_vec(&cdat.encoders_ptr, &cdat.count_encoders, &enc_ids)
            );

            auto* conn = &connectors[conn_id];
            conn->id = conn_id;
            require_prop_ids(conn_id, &conn->prop_ids);
            lookup_prop_ids(conn_id, &conn->opt_ids);
            switch (cdat.connector_type) {
#define T(x) case DRM_MODE_CONNECTOR_##x: conn->name = #x; break
              T(Unknown);
              T(VGA);
              T(DVII);
              T(DVID);
              T(DVIA);
              T(Composite);
              T(SVIDEO);
              T(LVDS);
              T(Component);
              T(9PinDIN);
              T(DisplayPort);
              T(HDMIA);
              T(HDMIB);
              T(TV);
              T(eDP);
              T(VIRTUAL);
              T(DSI);
              T(DPI);
              T(WRITEBACK);
              T(SPI);
              T(USB);
#undef T
              default: conn->name = fmt::format("[#{}]", cdat.connector_type);
            }
            conn->name += fmt::format("-{}", cdat.connector_type_id);

            for (auto const enc_id : enc_ids) {
                drm_mode_get_encoder edat = {};
                edat.encoder_id = enc_id;
                fd->ioc<DRM_IOCTL_MODE_GETENCODER>(&edat).ex("DRM encoder");
                for (size_t i = 0; i < crtc_ids.size(); ++i) {
                    if (edat.possible_crtcs & (1 << i)) {
                        auto* const crtc = &crtcs[crtc_ids[i]];
                        conn->usable_crtcs.push_back(crtc);
                    }
                }
            }
        }

        drm_mode_get_plane_res pres = {};
        std::vector<uint32_t> plane_ids;
        do {
            fd->ioc<DRM_IOCTL_MODE_GETPLANERESOURCES>(&pres).ex("DRM planes");
        } while (size_vec(&pres.plane_id_ptr, &pres.count_planes, &plane_ids));

        for (auto const plane_id : plane_ids) {
            drm_mode_get_plane pdat = {};
            pdat.plane_id = plane_id;
            fd->ioc<DRM_IOCTL_MODE_GETPLANE>(&pdat).ex("DRM plane");

            auto* plane = &planes[plane_id];
            plane->id = plane_id;
            require_prop_ids(plane_id, &plane->prop_ids);
            for (size_t i = 0; i < crtc_ids.size(); ++i) {
                if (pdat.possible_crtcs & (1 << i))
                    crtcs[crtc_ids[i]].usable_planes.push_back(plane);
            }

            if (plane->IN_FORMATS.init_value) {
                drm_mode_get_blob bdat = {};
                bdat.blob_id = plane->IN_FORMATS.init_value;
                std::vector<uint8_t> blob_data;
                do {
                    fd->ioc<DRM_IOCTL_MODE_GETPROPBLOB>(&bdat).ex("DRM blob");
                } while (size_vec(&bdat.data, &bdat.length, &blob_data));

                auto const* blob_bytes = blob_data.data();
                auto const* blob = (drm_format_modifier_blob const*) blob_bytes;
                if (blob->version == FORMAT_BLOB_CURRENT) {
                    auto const* fmt_bytes = blob_bytes + blob->formats_offset;
                    auto const* fmts = (uint32_t const*) fmt_bytes;
                    plane->formats.insert(fmts, fmts + blob->count_formats);
                }
            }
        }
    }

  private:
    struct PropId {
        using Map = std::map<std::string_view, PropId*>;
        PropId(std::string_view n, Map* map) { (*map)[n] = this; }
        uint32_t prop_id = 0;     // Filled by open() -> lookup_prop_ids()
        uint64_t init_value = 0;  // The property value at open() time
    };

    struct Writeback {
        ImageBuffer image;
        std::shared_ptr<uint32_t const> fb_id;
        std::shared_ptr<FileDescriptor> fence;
    };

    struct Crtc;
    struct Plane {
        uint32_t id = 0;
        std::set<uint32_t> formats;
        PropId::Map prop_ids;
        PropId CRTC_ID{"CRTC_ID", &prop_ids};
        PropId CRTC_X{"CRTC_X", &prop_ids};
        PropId CRTC_Y{"CRTC_Y", &prop_ids};
        PropId CRTC_W{"CRTC_W", &prop_ids};
        PropId CRTC_H{"CRTC_H", &prop_ids};
        PropId FB_ID{"FB_ID", &prop_ids};
        PropId IN_FORMATS{"IN_FORMATS", &prop_ids};
        PropId SRC_X{"SRC_X", &prop_ids};
        PropId SRC_Y{"SRC_Y", &prop_ids};
        PropId SRC_W{"SRC_W", &prop_ids};
        PropId SRC_H{"SRC_H", &prop_ids};
        PropId type{"type", &prop_ids};

        Crtc* used_by_crtc = nullptr;
    };

    struct Connector;
    struct Crtc {
        struct State {
            std::vector<Plane*> using_planes;
            std::vector<std::shared_ptr<uint32_t const>> images;
            drm_mode_modeinfo mode = {};
            std::optional<Writeback> writeback;
        };

        uint32_t id = 0;
        std::vector<Plane*> usable_planes;
        PropId::Map prop_ids;
        PropId ACTIVE{"ACTIVE", &prop_ids};
        PropId MODE_ID{"MODE_ID", &prop_ids};

        Connector* used_by_conn = nullptr;
        State active;
        std::optional<State> pending_flip;
    };

    struct Connector {
        uint32_t id = 0;
        std::string name;
        std::vector<Crtc*> usable_crtcs;
        PropId::Map prop_ids, opt_ids;
        PropId CRTC_ID{"CRTC_ID", &prop_ids};
        PropId WRITEBACK_FB_ID{"WRITEBACK_FB_ID", &opt_ids};
        PropId WRITEBACK_OUT_FENCE_PTR{"WRITEBACK_OUT_FENCE_PTR", &opt_ids};

        Crtc* using_crtc = nullptr;
    };

    std::shared_ptr<spdlog::logger> const log = display_logger();
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<FileDescriptor> fd;
    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> connectors;
    std::map<uint32_t, std::string> prop_names;

    void process_events() {
        drm_event_vblank ev = {};
        for (;;) {
            log->trace("Checking for DRM events...");
            auto const ret = fd->read(&ev, sizeof(ev));
            if (ret.err == EAGAIN) {
                log->trace("> (no DRM events pending)");
                break;
            }

            if (ret.ex("Read DRM event") != sizeof(ev))
                throw std::runtime_error("Bad DRM event size");
            if (ev.base.type != DRM_EVENT_FLIP_COMPLETE) {
                log->trace("> (got other DRM event, ignoring)");
                continue;
            }

            auto* const crtc = &crtcs.at(ev.crtc_id);
            if (!crtc->pending_flip) {
                throw std::runtime_error(
                    fmt::format("Unexpected DRM CRTC flipped ({})", crtc->id)
                );
            }

            auto* conn = crtc->used_by_conn;
            assert(conn);

            log->debug("Update complete (vsync flip): {}", conn->name);
            if (!crtc->pending_flip->mode.vrefresh) {
                assert(conn->using_crtc == crtc);
                conn->using_crtc = nullptr;
                crtc->used_by_conn = nullptr;
            }

            for (auto* plane : crtc->active.using_planes) {
                assert(plane->used_by_crtc == crtc);
                plane->used_by_crtc = nullptr;
            }

            for (auto* plane : crtc->pending_flip->using_planes) {
                assert(plane->used_by_crtc == nullptr);
                plane->used_by_crtc = crtc;
            }

            crtc->active = std::move(*crtc->pending_flip);
            crtc->pending_flip.reset();
        }
    }

    void require_prop_ids(uint32_t obj_id, PropId::Map* map) {
        lookup_prop_ids(obj_id, map);
        for (auto const name_propid : *map) {
            if (name_propid.second->prop_id) continue;
            throw std::runtime_error(fmt::format(
                "DRM object #{} missing property \"{}\"",
                obj_id, name_propid.first
            ));
        }
    }

    void lookup_prop_ids(uint32_t obj_id, PropId::Map* map) {
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> values;
        drm_mode_obj_get_properties odat = {};
        odat.obj_id = obj_id;
        do {
            fd->ioc<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&odat).ex("Properties");
        } while (
            size_vec(&odat.props_ptr, &odat.count_props, &prop_ids) +
            size_vec(&odat.prop_values_ptr, &odat.count_props, &values)
        );

        assert(prop_ids.size() == values.size());
        for (size_t i = 0; i < prop_ids.size(); ++i) {
            auto const prop_id = prop_ids[i];
            auto id_name_iter = prop_names.find(prop_id);
            if (id_name_iter == prop_names.end()) {
                drm_mode_get_property pdat = {};
                pdat.prop_id = prop_id;
                fd->ioc<DRM_IOCTL_MODE_GETPROPERTY>(&pdat).ex("Property");
                id_name_iter = prop_names.insert({prop_id, pdat.name}).first;
            }

            auto const name_propid_iter = map->find(id_name_iter->second);
            if (name_propid_iter != map->end()) {
                name_propid_iter->second->prop_id = prop_id;
                name_propid_iter->second->init_value = values[i];
            }
        }
    }

    template <typename T>
    std::shared_ptr<uint32_t const> create_blob(T const& data) {
        static_assert(std::is_standard_layout<T>::value);
        drm_mode_create_blob cblob = {(uint64_t) &data, sizeof(data), 0};
        fd->ioc<DRM_IOCTL_MODE_CREATEPROPBLOB>(&cblob).ex("DRM blob");

        auto const fd = this->fd;
        auto const deleter = [fd](uint32_t* const id) {
            drm_mode_destroy_blob dblob = {.blob_id = *id};
            delete id;
            (void) fd->ioc<DRM_IOCTL_MODE_DESTROYPROPBLOB>(&dblob);
        };
        return {new uint32_t(cblob.blob_id), deleter};
    }

    DrmDriver(DrmDriver const&) = delete;
    DrmDriver& operator=(DrmDriver const&) = delete;
};

}  // anonymous namespace

//
// Driver scanning and opening
//

std::vector<DisplayDriverListing> list_display_drivers(
    std::shared_ptr<UnixSystem> const& sys
) {
    std::vector<DisplayDriverListing> out;

    std::string const dri_dir = "/dev/dri";
    for (auto const& fname : sys->list(dri_dir).ex(dri_dir)) {
        if (fname.substr(0, 4) != "card" || !isdigit(fname[4])) continue;

        DisplayDriverListing listing;
        listing.dev_file = fmt::format("{}/{}", dri_dir, fname);

        struct stat fstat = sys->stat(listing.dev_file).ex(listing.dev_file);
        if ((fstat.st_mode & S_IFMT) != S_IFCHR) {
            throw std::system_error(
                std::make_error_code(std::errc::no_such_device),
                listing.dev_file
            );
        }

        auto const fd = sys->open(listing.dev_file, O_RDWR).ex(fname);
        drm_mode_card_res res = {};
        auto const ret = fd->ioc<DRM_IOCTL_MODE_GETRESOURCES>(&res);
        if (ret.err == ENOTSUP) continue;  // Not a KMS driver.
        ret.ex("DRM resource probe");

        auto const maj = major(fstat.st_rdev), min = minor(fstat.st_rdev);
        auto const dev_link = fmt::format("/sys/dev/char/{}:{}", maj, min);
        auto const sys_path = sys->realpath(dev_link).ex(dev_link);
        listing.system_path = (sys_path.substr(0, 13) == "/sys/devices/")
            ? sys_path.substr(13) : sys_path;

        // See kernel.org/doc/html/v5.10/gpu/drm-uapi.html
        drm_set_version set_ver = {1, 4, -1, -1};
        (void) fd->ioc<DRM_IOCTL_SET_VERSION>(&set_ver);

        std::vector<char> name, date, desc;
        drm_version ver = {};
        do {
            fd->ioc<DRM_IOCTL_VERSION>(&ver).ex("Get version");
        } while (
            size_vec(&ver.name, &ver.name_len, &name) +
            size_vec(&ver.date, &ver.date_len, &date) +
            size_vec(&ver.desc, &ver.desc_len, &desc)
        );
        listing.driver.assign(name.begin(), name.end());
        listing.driver_date.assign(date.begin(), date.end());
        listing.driver_desc.assign(desc.begin(), desc.end());

        std::vector<char> bus_id;
        drm_unique uniq = {};
        do {
            fd->ioc<DRM_IOCTL_GET_UNIQUE>(&uniq).ex("Get unique");
        } while (size_vec(&uniq.unique, &uniq.unique_len, &bus_id));
        listing.driver_bus_id.assign(bus_id.begin(), bus_id.end());
        out.push_back(std::move(listing));
    }

    return out;
}

std::unique_ptr<DisplayDriver> open_display_driver(
    std::shared_ptr<UnixSystem> sys, std::string const& dev
) {
    auto driver = std::make_unique<DrmDriver>();
    driver->open(std::move(sys), dev);
    return driver;
}

//
// Debugging utilities 
//

std::string debug(DisplayDriverListing const& d) {
    return fmt::format(
        "{} ({}): {}{}",
        d.dev_file, d.driver, d.system_path,
        d.driver_bus_id.empty() ? "" : fmt::format(" ({})", d.driver_bus_id)
    );
}

std::string debug(DisplayMode const& m) {
    return fmt::format(
        "{:5.1f}MHz{} {:3}[{:3}{}]{:<3} {:>4}x"
        "{:4}{} {:2}[{:2}{}]{:<2} {:2}Hz \"{}\"",
        m.pixel_khz / 1024.0,
        m.horiz.doubling > 0 ? "*2" : m.horiz.doubling < 0 ? "/2" : "  ",
        m.horiz.sync_start - m.horiz.display,
        m.horiz.sync_end - m.horiz.sync_start,
        m.horiz.sync_polarity < 0 ? "-" : m.horiz.sync_polarity > 0 ? "+" : "",
        m.horiz.total - m.horiz.sync_end,
        m.horiz.display,
        m.vert.display,
        m.vert.doubling > 0 ? "p2" : m.vert.doubling < 0 ? "i " : "p ",
        m.vert.sync_start - m.vert.display,
        m.vert.sync_end - m.vert.sync_start,
        m.vert.sync_polarity < 0 ? "-" : m.vert.sync_polarity > 0 ? "+" : " ",
        m.vert.total - m.vert.sync_end,
        m.refresh_hz,
        m.name
    );
}

}  // namespace pivid
