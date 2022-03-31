#include "display_output.h"

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
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <type_traits>

#include <fmt/core.h>

#include "logging_policy.h"
#include "unix_system.h"

namespace pivid {

namespace {

auto const& display_logger() {
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
        .size = {drm.hdisplay, drm.vdisplay},
        .scan_size = {drm.htotal, drm.vtotal},
        .sync_start = {drm.hsync_start, drm.vsync_start},
        .sync_end = {drm.hsync_end, drm.vsync_end},
        .sync_polarity = {
            sign(DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PHSYNC),
            sign(DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_PVSYNC),
        },
        .doubling = {
            sign(DRM_MODE_FLAG_CLKDIV2, DRM_MODE_FLAG_DBLCLK),
            sign(DRM_MODE_FLAG_INTERLACE, DRM_MODE_FLAG_DBLSCAN),
        },
        .pixel_khz = int(drm.clock), 
        .nominal_hz = int(drm.vrefresh), 
    };
}

drm_mode_modeinfo mode_to_drm(DisplayMode const& mode) {
    drm_mode_modeinfo out = {
        .clock = uint32_t(mode.pixel_khz),
        .hdisplay = uint16_t(mode.size.x),
        .hsync_start = uint16_t(mode.sync_start.x),
        .hsync_end = uint16_t(mode.sync_end.x),
        .htotal = uint16_t(mode.scan_size.x),
        .hskew = 0,
        .vdisplay = uint16_t(mode.size.y),
        .vsync_start = uint16_t(mode.sync_start.y),
        .vsync_end = uint16_t(mode.sync_end.y),
        .vtotal = uint16_t(mode.scan_size.y),
        .vscan = uint16_t(mode.doubling.y ? 2 : 1),
        .vrefresh = uint32_t(mode.nominal_hz),
        .flags = uint32_t(
            (mode.sync_polarity.x > 0 ? DRM_MODE_FLAG_PHSYNC : 0) |
            (mode.sync_polarity.x < 0 ? DRM_MODE_FLAG_NHSYNC : 0) |
            (mode.sync_polarity.y > 0 ? DRM_MODE_FLAG_PVSYNC : 0) |
            (mode.sync_polarity.y < 0 ? DRM_MODE_FLAG_NVSYNC : 0) |
            (mode.doubling.y < 0 ? DRM_MODE_FLAG_INTERLACE : 0) |
            (mode.doubling.y > 0 ? DRM_MODE_FLAG_DBLSCAN : 0) |
            (mode.doubling.x > 0 ? DRM_MODE_FLAG_DBLCLK : 0) |
            (mode.doubling.x < 0 ? DRM_MODE_FLAG_CLKDIV2 : 0)
        ),
        .type = uint32_t(DRM_MODE_TYPE_USERDEF),
        .name = {},
    };

    strncpy(out.name, mode.name.c_str(), sizeof(out.name) - 1);  // NUL term.
    return out;
}

uint32_t format_to_drm(uint32_t format) {
    // Note, ffmpeg/AVI/"standard" fourcc is big endian, DRM is little endian
    // We use "rgb" for premultiplied-alpha components, which DRM expects.
    switch (format) {
        case fourcc("0BGR"): return DRM_FORMAT_RGBX8888;
        case fourcc("0RGB"): return DRM_FORMAT_BGRX8888;
        case fourcc("Abgr"): return DRM_FORMAT_RGBA8888;
        case fourcc("Argb"): return DRM_FORMAT_BGRA8888;
        case fourcc("BGR0"): return DRM_FORMAT_XRGB8888;
        case fourcc("bgrA"): return DRM_FORMAT_ARGB8888;
        case fourcc("BGR\x10"): return DRM_FORMAT_BGR565;
        case fourcc("BGR\x18"): return DRM_FORMAT_RGB888;
        case fourcc("I420"): return DRM_FORMAT_YUV420;
        case fourcc("NV12"): return DRM_FORMAT_NV12;
        case fourcc("NV21"): return DRM_FORMAT_NV21;
        case fourcc("PAL\x08"): return DRM_FORMAT_C8;
        case fourcc("RGB0"): return DRM_FORMAT_XBGR8888;
        case fourcc("rgbA"): return DRM_FORMAT_ABGR8888;
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
    DrmDumbBuffer(std::shared_ptr<FileDescriptor> fd, XY<int> size, int bpp) {
        ddat.height = size.y;
        ddat.width = size.x;
        ddat.bpp = bpp;
        fd->ioc<DRM_IOCTL_MODE_CREATE_DUMB>(&ddat).ex("DRM buffer");
        this->fd = std::move(fd);
    }

    virtual ~DrmDumbBuffer() final {
        if (!ddat.handle) return;
        drm_mode_destroy_dumb dddat = {.handle = ddat.handle};
        (void) fd->ioc<DRM_IOCTL_MODE_DESTROY_DUMB>(&dddat);
    }

    virtual size_t size() const final { return ddat.size; }

    virtual uint8_t const* read() final {
        std::scoped_lock const lock{mem_mutex};
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

    virtual uint32_t drm_handle() const final { return ddat.handle; }
    uint8_t* write() { read(); return (uint8_t*) mem.get(); }
    ptrdiff_t stride() const { return ddat.pitch; }

  private:
    std::shared_ptr<FileDescriptor> fd;
    drm_mode_create_dumb ddat = {};
    std::mutex mem_mutex;
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

class LoadedImageDef : public LoadedImage {
  public:
    LoadedImageDef(std::shared_ptr<FileDescriptor> fd, ImageBuffer const& im) {
        fdat.width = im.size.x;
        fdat.height = im.size.y;
        fdat.pixel_format = format_to_drm(im.fourcc);
        fdat.flags = DRM_MODE_FB_MODIFIERS;

        size_t const max_channels = std::extent_v<decltype(fdat.handles)>;
        CHECK_ARG(
            im.channels.size() <= max_channels,
            "Too many image channels ({}) for DRM", im.channels.size()
        );

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

        auto const logger = display_logger();
        TRACE(logger, "Creating DRM framebuffer...");
        this->fd = std::move(fd);
        this->fd->ioc<DRM_IOCTL_MODE_ADDFB2>(&fdat).ex("DRM framebuffer");
        DEBUG(logger, "Loaded fb{} {}", fdat.fb_id, debug(im));
    }

    ~LoadedImageDef() {
        if (!fdat.fb_id) return;
        auto const logger = display_logger();
        TRACE(logger, "Removing DRM framebuffer...");
        (void) fd->ioc<DRM_IOCTL_MODE_RMFB>(&fdat.fb_id);
        logger->debug("Unload fb{} {}x{}", fdat.fb_id, fdat.width, fdat.height);
    }

    virtual uint32_t drm_id() const final { return fdat.fb_id; }

    virtual XY<int> size() const final {
        return XY<int>(fdat.width, fdat.height);
    }

  private:
    std::shared_ptr<FileDescriptor> fd;
    drm_mode_fb_cmd2 fdat = {};

    LoadedImageDef(LoadedImageDef const&) = delete;
    LoadedImageDef& operator=(LoadedImageDef const&) = delete;
};

//
// DisplayDriver implementation
//

class DisplayDriverDef : public DisplayDriver {
  public:
    DisplayDriverDef() {}

    virtual std::vector<DisplayScreen> scan_screens() final {
        TRACE(logger, "Scanning screens...");
        std::vector<DisplayScreen> out;
        for (auto const& id_conn : connectors) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                cdat.count_props = cdat.count_encoders = 0;
                fd->ioc<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat).ex("DRM connector");
            } while (size_vec(&cdat.modes_ptr, &cdat.count_modes, &modes));

            DisplayScreen screen = {};
            screen.id = id_conn.first;
            screen.connector = id_conn.second.name;
            screen.display_detected = (cdat.connection == 1);

            for (auto const& mode : modes) {
                if (!(mode.flags & DRM_MODE_FLAG_3D_MASK))
                    screen.modes.push_back(mode_from_drm(mode));
            }

            if (cdat.encoder_id) {
                drm_mode_get_encoder edat = {};
                edat.encoder_id = cdat.encoder_id;
                fd->ioc<DRM_IOCTL_MODE_GETENCODER>(&edat).ex("DRM encoder");
                if (edat.crtc_id) {
                    // We are DRM master, assume no sneaky mode changes.
                    auto const& drm_mode = crtcs.at(edat.crtc_id).active.mode;
                    screen.active_mode = mode_from_drm(drm_mode);
                }
            }

            out.push_back(std::move(screen));
        }
        logger->debug("Found {} display screens", out.size());
        return out;
    }

    virtual std::unique_ptr<LoadedImage> load_image(ImageBuffer im) final {
        TRACE(
            logger, "Loading {}x{} {}...",
            im.size.x, im.size.y, debug_fourcc(im.fourcc)
        );

        switch (im.fourcc) {
            case fourcc("ABGR"):
            case fourcc("ARGB"):
            case fourcc("BGRA"):
            case fourcc("RGBA"): {
                TRACE(logger, "> (premultiplying alpha...)");
                CHECK_ARG(
                    im.channels.size() == 1,
                    "Bad channel count ({}) for {} image",
                    im.channels.size(), debug_fourcc(im.fourcc)
                );

                auto const& chan = im.channels[0];
                int const w = im.size.x, h = im.size.y;
                size_t const min_size = chan.offset + chan.stride * h;
                CHECK_ARG(
                    chan.memory->size() >= min_size && chan.stride >= 4 * w,
                    "Bad buffer size ({}/{}) for {}x{} {} @{}",
                    debug_size(chan.memory->size()),
                    debug_size(chan.stride), w, h, debug_fourcc(im.fourcc),
                    debug_size(chan.offset)
                );

                auto buf = std::make_shared<DrmDumbBuffer>(fd, im.size, 32);
                for (int y = 0; y < h; ++y) {
                    int const from_offset = y * chan.stride + chan.offset;
                    uint8_t const* from = chan.memory->read() + from_offset;
                    uint8_t* to = buf->write() + y * buf->stride();
                    to_premultiplied_rgba(im.fourcc, w, from, to);
                }

                im.fourcc = fourcc("rgbA");
                im.channels[0].offset = 0;
                im.channels[0].stride = buf->stride();
                im.channels[0].memory = std::move(buf);
                break;
            }

            case fourcc("PAL\x08"): {
                TRACE(logger, "> (expanding PAL8 to premultiplied rgbA...)");
                CHECK_ARG(
                    im.channels.size() == 2,
                    "Bad channel count ({}) for PAL8 image", im.channels.size()
                );

                auto const& chan = im.channels[0];
                int const w = im.size.x, h = im.size.y;
                size_t const min_size = chan.offset + chan.stride * h;
                CHECK_ARG(
                    chan.memory->size() >= min_size && chan.stride >= w,
                    "Bad buffer size ({}/{}) for {}x{} PAL8 @{}",
                    debug_size(chan.memory->size()),
                    debug_size(chan.stride), w, h, debug_size(chan.offset)
                );

                auto const& pch = im.channels[1];
                size_t const min_pch_size = pch.offset + 256 * 4;
                CHECK_ARG(
                    pch.memory->size() >= min_pch_size,
                    "Bad palette size ({}) for PAL8 image @{}",
                    debug_size(pch.memory->size()), debug_size(pch.offset)
                );

                // TODO: On big-endian, this would be ARGB.
                // https://ffmpeg.org/doxygen/3.3/pixfmt_8h.html
                uint8_t pal[256 * 4];
                to_premultiplied_rgba(
                    fourcc("BGRA"), 256, pch.memory->read() + pch.offset, pal
                );

                auto buf = std::make_shared<DrmDumbBuffer>(fd, im.size, 32);
                for (int y = 0; y < h; ++y) {
                    int const from_offset = y * chan.stride + chan.offset;
                    uint8_t const* from = chan.memory->read() + from_offset;
                    uint8_t* to = buf->write() + y * buf->stride();
                    for (int x = 0; x < w; ++x)
                        std::memcpy(to + 4 * x, pal + 4 * from[x], 4);
                }

                im.fourcc = fourcc("rgbA");
                im.channels.resize(1);
                im.channels[0].offset = 0;
                im.channels[0].stride = buf->stride();
                im.channels[0].memory = std::move(buf);
                break;
            }
        }

        for (size_t ci = 0; ci < im.channels.size(); ++ci) { 
            auto *chan = &im.channels[ci];
            if (chan->memory->dma_fd() >= 0 || chan->memory->drm_handle())
                continue;

            TRACE(logger, "> (copy {} ch{}...)", debug_fourcc(im.fourcc), ci);
            auto buf = std::make_shared<DrmDumbBuffer>(
                fd, im.size, 8 * chan->stride / im.size.x
            );

            for (int y = 0; y < im.size.y; ++y) {
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

        return std::make_unique<LoadedImageDef>(fd, std::move(im));
    }

    virtual void update(
        uint32_t screen_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) final {
        auto* const conn = &connectors.at(screen_id);
        TRACE(logger, "Starting {} update...", conn->name);

        std::scoped_lock const lock{mutex};
        auto* crtc = conn->using_crtc;
        CHECK_ARG(
            !crtc || !crtc->pending_flip,
            "Update requested before prev done"
        );

        if (!crtc && !mode.nominal_hz) {
            TRACE(logger, "> (output remains off, no change)");
            return;
        }

        if (!crtc) {
            for (auto* const c : conn->usable_crtcs) {
                if (c->used_by_conn) continue;
                ASSERT(!c->pending_flip);
                crtc = c;
                break;
            }
            CHECK_RUNTIME(crtc, "No DRM CRTC: {}", conn->name);
        }

        // Build the atomic update and the state that will result.
        std::map<uint32_t, std::map<PropId const*, uint64_t>> props;
        std::shared_ptr<uint32_t const> mode_blob;
        Crtc::State next = {};
        int32_t writeback_fence_fd = -1;

        if (!mode.nominal_hz) {
            logger->debug("{} :: [turning off]", conn->name);
            props[conn->id][&conn->CRTC_ID] = 0;
            props[crtc->id][&crtc->ACTIVE] = 0;
            // Leave next state zeroed.
        } else {
            next.mode = mode_to_drm(mode);
            static_assert(sizeof(crtc->active.mode) == sizeof(next.mode));
            if (memcmp(&crtc->active.mode, &next.mode, sizeof(next.mode))) {
                auto const old_mode = mode_from_drm(crtc->active.mode);
                DEBUG(logger, "{} OLD {}", conn->name, debug(old_mode));
                DEBUG(logger, "{} NEW {}", conn->name, debug(mode));
                mode_blob = create_blob(next.mode);
                props[crtc->id][&crtc->MODE_ID] = *mode_blob;
            } else {
                DEBUG(logger, "{} SAME {}", conn->name, debug(mode));
            }

            if (conn->WRITEBACK_FB_ID.prop_id) {
                XY<int> const size = {next.mode.hdisplay, next.mode.vdisplay};
                TRACE(logger, "Making {}x{} writeback...", size.x, size.y);
                auto buf = std::make_shared<DrmDumbBuffer>(fd, size, 32);

                Writeback wb = {};
                wb.image.fourcc = fourcc("RGBA");
                wb.image.size = size;
                wb.image.channels.resize(1);
                wb.image.channels[0].stride = buf->stride();
                wb.image.channels[0].memory = std::move(buf);
                wb.fb_id = load_image(wb.image);

                int const id = wb.fb_id->drm_id();
                logger->debug("{} >> fb{} {}", conn->name, id, debug(wb.image));
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
            for (size_t li = 0; li < layers.size(); ++li) {
                // Find an appropriate plane (Primary=1, Overlay=0)
                auto const& layer = layers[li];
                uint64_t const wanted_type = li ? 0 : 1;
                for (;; ++plane_iter) {
                    CHECK_RUNTIME(
                        plane_iter != crtc->usable_planes.end(),
                        "No DRM plane: {}", conn->name
                    );

                    if ((*plane_iter)->type.init_value != wanted_type) continue;
                    if ((*plane_iter)->used_by_crtc == crtc) break;
                    if (!(*plane_iter)->used_by_crtc) break;
                }

                auto* plane = *plane_iter++;
                int const fb_id = layer.image->drm_id();
                next.using_planes.push_back(plane);
                next.images.push_back(layer.image);

                logger->debug("{} << pl={} fb{}", conn->name, plane->id, fb_id);

                auto* plane_props = &props[plane->id];
                (*plane_props)[&plane->CRTC_ID] = crtc->id;
                (*plane_props)[&plane->FB_ID] = fb_id;

                (*plane_props)[&plane->SRC_X] = 65536.0 * layer.from_xy.x;
                (*plane_props)[&plane->SRC_Y] = 65536.0 * layer.from_xy.y;
                (*plane_props)[&plane->SRC_W] = 65536.0 * layer.from_size.x;
                (*plane_props)[&plane->SRC_H] = 65536.0 * layer.from_size.y;
                (*plane_props)[&plane->CRTC_X] = layer.to_xy.x;
                (*plane_props)[&plane->CRTC_Y] = layer.to_xy.y;
                (*plane_props)[&plane->CRTC_W] = layer.to_size.x;
                (*plane_props)[&plane->CRTC_H] = layer.to_size.y;

                if (plane->alpha.prop_id) {
                    (*plane_props)[&plane->alpha] = layer.opacity * 65535.0;
                } else {
                    CHECK_RUNTIME(layer.opacity >= 1.0, "Alpha unsupported");
                }
            }
        }

        std::vector<uint32_t> obj_ids;
        std::vector<uint32_t> obj_prop_counts;
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> prop_values;
        for (auto const& obj_props : props) {
            obj_ids.push_back(obj_props.first);
            obj_prop_counts.push_back(obj_props.second.size());
            for (auto const& prop_value : obj_props.second) {
               TRACE(logger, 
                   "> #{} {} = {}",
                   obj_props.first, prop_value.first->name, prop_value.second
               );
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

        TRACE(logger, "Committing DRM atomic update...");
        auto ret = fd->ioc<DRM_IOCTL_MODE_ATOMIC>(&atomic);
        if (ret.err == EBUSY) {
            TRACE(logger, "> (driver busy, retrying update without NONBLOCK)");
            atomic.flags &= ~DRM_MODE_ATOMIC_NONBLOCK;
            ret = fd->ioc<DRM_IOCTL_MODE_ATOMIC>(&atomic);
        }

        if (writeback_fence_fd >= 0) {
            TRACE(logger, "> (writeback fence fd={})", writeback_fence_fd);
            if (!next.writeback) next.writeback.emplace();
            next.writeback->fence = sys->adopt(writeback_fence_fd);
        }

        ret.ex("DRM atomic update");
        logger->debug("{} update committed", conn->name);
        crtc->pending_flip.emplace(std::move(next));
        for (auto* plane : crtc->pending_flip->using_planes) {
            ASSERT(plane->used_by_crtc == crtc || !plane->used_by_crtc);
            plane->used_by_crtc = crtc;
        }

        conn->using_crtc = crtc;
        crtc->used_by_conn = conn;
    }

    virtual std::optional<DisplayUpdateDone> is_update_done(uint32_t id) final {
        auto* const conn = &connectors.at(id);
        TRACE(logger, "Checking {} completion...", conn->name);

        std::scoped_lock const lock{mutex};
        if (conn->using_crtc && conn->using_crtc->pending_flip) {
            process_events(lock);
            if (conn->using_crtc && conn->using_crtc->pending_flip) {
                TRACE(logger, "> (previous update incomplete)");
                return {};
            }
        }

        DisplayUpdateDone done = {};
        if (!conn->using_crtc) {
            TRACE(logger, "> (output is off, no update pending)");
        } else if (conn->using_crtc->active.writeback) {
            done.writeback = conn->using_crtc->active.writeback->image;
            TRACE(logger, "> (update done with writeback)");
        } else {
            TRACE(logger, "> (update done)");
        }

        done.flip_time = conn->flip_time;
        return done;
    }

    void open(std::shared_ptr<UnixSystem> sys, std::string const& dev) {
        logger->info("Opening display \"{}\"...", dev);
        this->sys = std::move(sys);
        fd = this->sys->open(dev.c_str(), O_RDWR | O_NONBLOCK).ex(dev);
        try {
            fd->ioc<DRM_IOCTL_SET_MASTER>().ex("DRM master mode");
        } catch (std::system_error const& e) {
            logger->error("{}", e.what());
            // Continue, though something will probably fail later
        }

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
        std::vector<uint32_t> crtc_ids, conn_ids;
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
            lookup_required_prop_ids(crtc_id, &crtc->prop_ids);
            if (ccdat.mode_valid)  // Round-trip to ensure struct memcmp().
                crtc->active.mode = mode_to_drm(mode_from_drm(ccdat.mode));
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
            lookup_required_prop_ids(conn_id, &conn->prop_ids);
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
            lookup_required_prop_ids(plane_id, &plane->prop_ids);
            lookup_prop_ids(plane_id, &plane->opt_ids);
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

        logger->debug(
            "> opened fd={}: {} planes, {} crtcs, {} screen connectors",
            fd->raw_fd(), planes.size(), crtcs.size(), connectors.size()
        );
    }

  private:
    struct PropId {
        using Map = std::map<std::string_view, PropId*>;
        PropId(std::string_view n, Map* map) : name(n) { (*map)[n] = this; }
        std::string_view name;
        uint32_t prop_id = 0;     // Filled by open() -> lookup_prop_ids()
        uint64_t init_value = 0;  // The property value at open() time
    };

    struct Writeback {
        ImageBuffer image;
        std::shared_ptr<LoadedImage> fb_id;
        std::shared_ptr<FileDescriptor> fence;
    };

    struct Crtc;
    struct Plane {
        // Constant from setup to ~
        uint32_t id = 0;
        std::set<uint32_t> formats;
        PropId::Map prop_ids, opt_ids;
        PropId alpha{"alpha", &opt_ids};
        PropId CRTC_ID{"CRTC_ID", &prop_ids};
        PropId CRTC_X{"CRTC_X", &prop_ids};
        PropId CRTC_Y{"CRTC_Y", &prop_ids};
        PropId CRTC_W{"CRTC_W", &prop_ids};
        PropId CRTC_H{"CRTC_H", &prop_ids};
        PropId FB_ID{"FB_ID", &prop_ids};
        PropId IN_FORMATS{"IN_FORMATS", &prop_ids};
        PropId rotation{"rotation", &opt_ids};
        PropId SRC_X{"SRC_X", &prop_ids};
        PropId SRC_Y{"SRC_Y", &prop_ids};
        PropId SRC_W{"SRC_W", &prop_ids};
        PropId SRC_H{"SRC_H", &prop_ids};
        PropId type{"type", &prop_ids};

        // Guarded by DisplayDriverDef::mutex
        Crtc* used_by_crtc = nullptr;
    };

    struct Connector;
    struct Crtc {
        struct State {
            std::vector<Plane*> using_planes;
            std::vector<std::shared_ptr<LoadedImage>> images;
            drm_mode_modeinfo mode = {};
            std::optional<Writeback> writeback;
        };

        // Constant from setup to ~
        uint32_t id = 0;
        std::vector<Plane*> usable_planes;
        PropId::Map prop_ids;
        PropId ACTIVE{"ACTIVE", &prop_ids};
        PropId MODE_ID{"MODE_ID", &prop_ids};

        // Guarded by DisplayDriverDef::mutex
        Connector* used_by_conn = nullptr;
        State active;
        std::optional<State> pending_flip;
    };

    struct Connector {
        // Constant from setup to ~
        uint32_t id = 0;
        std::string name;
        std::vector<Crtc*> usable_crtcs;
        PropId::Map prop_ids, opt_ids;
        PropId CRTC_ID{"CRTC_ID", &prop_ids};
        PropId WRITEBACK_FB_ID{"WRITEBACK_FB_ID", &opt_ids};
        PropId WRITEBACK_OUT_FENCE_PTR{"WRITEBACK_OUT_FENCE_PTR", &opt_ids};

        // Guarded by DisplayDriverDef::mutex
        Crtc* using_crtc = nullptr;
        double flip_time;
    };

    // These containers are constant after startup (contained objects change)
    std::shared_ptr<log::logger> const logger = display_logger();
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<FileDescriptor> fd;

    std::mutex mutex;  // Guard for dynamic properties of objects below
    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> connectors;
    std::map<uint32_t, std::string> prop_names;

    void process_events(std::scoped_lock<std::mutex> const&) {
        TRACE(logger, "Checking for DRM events...");
        drm_event_vblank ev = {};
        for (;;) {
            auto const ret = fd->read(&ev, sizeof(ev));
            if (ret.err == EAGAIN) {
                TRACE(logger, "> (no DRM events pending)");
                break;
            }

            auto const ev_len = ret.ex("Read DRM event");
            CHECK_RUNTIME(ev_len == sizeof(ev), "Bad DRM event size");
            if (ev.base.type != DRM_EVENT_FLIP_COMPLETE) {
                TRACE(logger, "> (ignoring non-pageflip DRM event)");
                continue;
            }

            auto* const crtc = &crtcs.at(ev.crtc_id);
            CHECK_RUNTIME(
                crtc->pending_flip && crtc->used_by_conn,
                "Unexpected DRM CRTC pageflip ({})", crtc->id
            );

            auto* conn = crtc->used_by_conn;
            ASSERT(conn);

            // TODO: Check for writeback fence, once it works
            // (see https://forums.raspberrypi.com/viewtopic.php?t=328068)

            // TODO: Convert to system time!
            conn->flip_time = ev.tv_sec + 1e-6 * ev.tv_usec;

            DEBUG(
                logger, "Update pageflip: {} {:.3f}s",
                conn->name, conn->flip_time
            );

            if (!crtc->pending_flip->mode.vrefresh) {
                ASSERT(conn->using_crtc == crtc);
                conn->using_crtc = nullptr;
                crtc->used_by_conn = nullptr;
            }

            for (auto* plane : crtc->pending_flip->using_planes) {
                ASSERT(plane->used_by_crtc == crtc);
            }

            for (auto* plane : crtc->active.using_planes) {
                ASSERT(plane->used_by_crtc == crtc);
                plane->used_by_crtc = nullptr;
            }

            crtc->active = std::move(*crtc->pending_flip);
            crtc->pending_flip.reset();
        }
    }

    void lookup_required_prop_ids(uint32_t obj_id, PropId::Map* map) {
        lookup_prop_ids(obj_id, map);
        for (auto const name_propid : *map) {
            CHECK_RUNTIME(
                name_propid.second->prop_id,
                "DRM object #{} missing property \"{}\"",
                obj_id, name_propid.first
            );
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

        CHECK_RUNTIME(
            prop_ids.size() == values.size(),
            "Property list length mismatch"
        );

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

    DisplayDriverDef(DisplayDriverDef const&) = delete;
    DisplayDriverDef& operator=(DisplayDriverDef const&) = delete;
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
        CHECK_RUNTIME(
            (fstat.st_mode & S_IFMT) == S_IFCHR,
            "Not a character device node: {}", listing.dev_file
        );

        std::unique_ptr<FileDescriptor> fd;
        try {
            fd = sys->open(listing.dev_file, O_RDWR).ex(listing.dev_file);
        } catch (std::runtime_error const& e) {  // Skip but log on open error
            display_logger()->error("{}", e.what());
            continue;
        }

        drm_mode_card_res res = {};
        auto const res_ret = fd->ioc<DRM_IOCTL_MODE_GETRESOURCES>(&res);
        if (res_ret.err == ENOTSUP) continue;  // Not a KMS driver.
        res_ret.ex("DRM resource probe");

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
    auto driver = std::make_unique<DisplayDriverDef>();
    driver->open(std::move(sys), dev);
    return driver;
}

//
// Debugging utilities 
//

double DisplayMode::actual_hz() const {
    if (!scan_size.x || !scan_size.y) return 0;
    double const raw_hz = pixel_khz * 1000.0 / scan_size.x / scan_size.y;
    return raw_hz * (doubling.y < 0 ? 2.0 : doubling.y > 0 ? 0.5 : 1.0);
}

std::string debug(DisplayDriverListing const& d) {
    return fmt::format(
        "{} ({}): {}{}",
        d.dev_file, d.driver, d.system_path,
        d.driver_bus_id.empty() ? "" : fmt::format(" ({})", d.driver_bus_id)
    );
}

std::string debug(DisplayMode const& m) {
    if (!m.nominal_hz) return "OFF";
    return fmt::format(
        "{:5.1f}MHz{} {:3}[{:3}{}]{:<3} {:>4}x"
        "{:4}{} {:2}[{:2}{}]{:<2} {:6.3f}Hz \"{}\"",
        m.pixel_khz / 1000.0,
        m.doubling.x > 0 ? "*2" : m.doubling.x < 0 ? "/2" : "  ",
        m.sync_start.x - m.size.x,
        m.sync_end.x - m.sync_start.x,
        m.sync_polarity.x < 0 ? "-" : m.sync_polarity.x > 0 ? "+" : "",
        m.scan_size.x - m.sync_end.x,
        m.size.x,
        m.size.y,
        m.doubling.y > 0 ? "p2" : m.doubling.y < 0 ? "i " : "p ",
        m.sync_start.y - m.size.y,
        m.sync_end.y - m.sync_start.y,
        m.sync_polarity.y < 0 ? "-" : m.sync_polarity.y > 0 ? "+" : " ",
        m.scan_size.y - m.sync_end.y,
        m.actual_hz(),
        m.name
    );
}

}  // namespace pivid
