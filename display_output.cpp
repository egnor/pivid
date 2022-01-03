#include "display_output.h"

#undef NDEBUG
#include <assert.h>
#include <drm/drm.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <system_error>
#include <type_traits>

#include <fmt/core.h>

#include "unix_system.h"

namespace pivid {

namespace {

using strview = std::string_view;

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
        .pixel_khz = int(drm.clock), 
        .refresh_hz = int(drm.vrefresh), 
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

//
// DisplayDriver implementation
//

class DrmDriver : public DisplayDriver {
  public:
    void open(UnixSystem* sys, std::string const& dev) {
        fd = sys->open(dev.c_str(), O_RDWR | O_NONBLOCK).ex(dev);
        fd->ioc<DRM_IOCTL_SET_MASTER>().ex("Become DRM master");
        fd->ioc<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_ATOMIC, 1}
        ).ex("Enable DRM atomic modesetting");
        fd->ioc<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1}
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
            crtcs[crtc_id].id = crtc_id;
            lookup_prop_ids(crtc_id, &crtcs[crtc_id].prop_ids);
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
            lookup_prop_ids(conn_id, &conn->prop_ids);
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
            lookup_prop_ids(plane_id, &plane->prop_ids);
            for (size_t i = 0; i < crtc_ids.size(); ++i) {
                if (pdat.possible_crtcs & (1 << i))
                    crtcs[crtc_ids[i]].usable_planes.push_back(plane);
            }
        }
    }

    virtual std::vector<DisplayStatus> scan_outputs() {
        std::vector<DisplayStatus> out;
        for (auto const& id_conn : connectors) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                cdat.count_props = cdat.count_encoders = 0;
                fd->ioc<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat).ex("DRM connector");
            } while (size_vec(&cdat.modes_ptr, &cdat.count_modes, &modes));

            DisplayStatus status = {};
            status.connector_id = id_conn.first;
            status.connector_name = id_conn.second.name;
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
                    drm_mode_crtc ccdat = {};
                    ccdat.crtc_id = edat.crtc_id;
                    fd->ioc<DRM_IOCTL_MODE_GETCRTC>(&ccdat).ex("DRM CRTC");
                    if (ccdat.mode_valid)
                        status.active_mode = mode_from_drm(ccdat.mode);
                }
            }

            out.push_back(std::move(status));
        }
        return out;
    }

    virtual bool ready_for_update(uint32_t connector_id) {
        drm_event_vblank ev = {};
        for (;;) {
            auto const ret = fd->read(&ev, sizeof(ev));
            if (ret.err == EAGAIN) break;
            if (ret.ex("Read DRM event") != sizeof(ev))
                throw std::runtime_error("Bad DRM event size");

            if (ev.base.type != DRM_EVENT_FLIP_COMPLETE) continue;
            auto const id_crtc_iter = crtcs.find(ev.crtc_id);
            if (id_crtc_iter == crtcs.end()) {
                throw std::runtime_error(
                    fmt::format("Unknown CRTC flipped ({})", ev.crtc_id)
                );
            }

            auto* const crtc = &id_crtc_iter->second;
            if (!crtc->pending_flip) {
                throw std::runtime_error(
                    fmt::format("Unexpected CRTC flipped ({})", ev.crtc_id)
                );
            }

            if (crtc->used_by_conn && !crtc->pending_flip->mode.refresh_hz) {
                assert(crtc->used_by_conn->using_crtc == crtc);
                crtc->used_by_conn->using_crtc = nullptr;
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

        auto const id_conn_iter = connectors.find(connector_id);
        if (id_conn_iter == connectors.end())
            throw std::invalid_argument("Bad connector ID");

        auto const& conn = id_conn_iter->second;
        if (conn.using_crtc)
            return !conn.using_crtc->pending_flip;

        for (auto const* const crtc : conn.usable_crtcs) {
            if (!crtc->active.mode.refresh_hz && !crtc->pending_flip)
                return true;
        }

        return false;  // No CRTC available for this connector (yet?).
    }

    virtual void update_output(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) {
        auto const id_conn_iter = connectors.find(connector_id);
        if (id_conn_iter == connectors.end())
            throw std::invalid_argument("Bad connector ID");

        auto* const conn = &id_conn_iter->second;
        auto* crtc = conn->using_crtc;
        if (crtc) {
            if (crtc->pending_flip)
                throw std::invalid_argument("Set while pending: " + conn->name);
        } else {
            if (!mode.refresh_hz) return;  // Was off, still off
            for (auto* const c : conn->usable_crtcs) {
                if (!c->active.mode.refresh_hz && !c->pending_flip) {
                    crtc = c;
                    break;
                }
            }
            if (!crtc) throw std::runtime_error("CRTCs busy: " + conn->name);
        }

        // Build the atomic update and the state that will result.
        std::map<uint32_t, std::map<PropId const*, uint64_t>> obj_props;
        std::shared_ptr<uint32_t const> mode_blob;
        Crtc::State next = {};

        if (!mode.refresh_hz) {
            obj_props[conn->id][&conn->CRTC_ID] = 0;
            obj_props[crtc->id][&crtc->ACTIVE] = 0;
            // Leave next state zeroed.
        } else {
            if (!conn->using_crtc) {
                obj_props[conn->id][&conn->CRTC_ID] = crtc->id;
                obj_props[crtc->id][&crtc->ACTIVE] = 1;
            }

            next.mode = mode;
            if (crtc->active.mode != next.mode) {
                mode_blob = create_blob(mode_to_drm(next.mode));
                obj_props[crtc->id][&crtc->MODE_ID] = *mode_blob;
            }

            auto plane_iter = crtc->usable_planes.begin();
            for (auto const& layer : layers) {
                // Find an appropriate plane (Primary=1, Overlay=0)
                uint64_t const wanted_type = (&layer == &layers[0] ? 1 : 0);
                for (;; ++plane_iter) {
                    if (plane_iter == crtc->usable_planes.end())
                        throw std::runtime_error("Planes busy: " + conn->name);

                    if ((*plane_iter)->type.init_value != wanted_type) continue;
                    if ((*plane_iter)->used_by_crtc == crtc) break;
                    if (!(*plane_iter)->used_by_crtc) break;
                }

                auto const* plane = *plane_iter++;
                if (plane->used_by_crtc != crtc)
                    obj_props[plane->id][&plane->CRTC_ID] = crtc->id;

                auto fb_it = crtc->active.fb_ids.find(layer.fb);
                if (fb_it != crtc->active.fb_ids.end()) {
                    fb_it = next.fb_ids.insert(*fb_it).first;
                } else {
                    auto const fb = create_fb(layer.fb);
                    fb_it = next.fb_ids.insert({layer.fb, fb}).first;
                }

                obj_props[plane->id][&plane->FB_ID] = *fb_it->second;

                obj_props[plane->id][&plane->CRTC_X] = layer.out_x;
                obj_props[plane->id][&plane->CRTC_Y] = layer.out_y;
                obj_props[plane->id][&plane->CRTC_W] = layer.out_width;
                obj_props[plane->id][&plane->CRTC_H] = layer.out_height;

                auto fixed = [](double d) -> int64_t { return d * 65536.0; };
                obj_props[plane->id][&plane->SRC_X] = fixed(layer.fb_x);
                obj_props[plane->id][&plane->SRC_Y] = fixed(layer.fb_y);
                obj_props[plane->id][&plane->SRC_W] = fixed(layer.fb_width);
                obj_props[plane->id][&plane->SRC_H] = fixed(layer.fb_height);
            }
        }

        std::vector<uint32_t> obj_ids;
        std::vector<uint32_t> obj_prop_counts;
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> prop_values;
        for (auto const& obj_prop : obj_props) {
            obj_ids.push_back(obj_prop.first);
            obj_prop_counts.push_back(obj_prop.second.size());
            for (auto const& prop_value : obj_prop.second) {
               prop_ids.push_back(prop_value.first->prop_id);
               prop_values.push_back(prop_value.second);
            }
        }

        drm_mode_atomic atomic = {
            .flags = DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET,
            .count_objs = obj_ids.size(),
            .objs_ptr = (uint64_t) obj_ids.data(),
            .count_props_ptr = (uint64_t) obj_prop_counts.data(),
            .props_ptr = (uint64_t) prop_ids.data(),
            .prop_values_ptr = (uint64_t) prop_values.data(),
            .reserved = 0,
            .user_data = 0,
        };

        fd->ioc<DRM_IOCTL_MODE_ATOMIC>(&atomic).ex("Atomic DRM update");
        crtc->pending_flip.emplace(std::move(next));
        conn->using_crtc = crtc;
        crtc->used_by_conn = conn;
    }

  private:
    struct PropId {
        PropId(strview n, std::map<strview, PropId*>* map) { (*map)[n] = this; }
        uint32_t prop_id = 0;     // Filled by open() -> lookup_prop_ids()
        uint64_t init_value = 0;  // The property value at open() time
    };

    struct Plane;
    struct Crtc;
    struct Connector;

    struct Plane {
        uint32_t id = 0;
        std::map<strview, PropId*> prop_ids;
        PropId CRTC_ID{"CRTC_ID", &prop_ids};
        PropId CRTC_X{"CRTC_X", &prop_ids};
        PropId CRTC_Y{"CRTC_Y", &prop_ids};
        PropId CRTC_W{"CRTC_W", &prop_ids};
        PropId CRTC_H{"CRTC_H", &prop_ids};
        PropId FB_ID{"FB_ID", &prop_ids};
        PropId SRC_X{"SRC_X", &prop_ids};
        PropId SRC_Y{"SRC_Y", &prop_ids};
        PropId SRC_W{"SRC_W", &prop_ids};
        PropId SRC_H{"SRC_H", &prop_ids};
        PropId type{"type", &prop_ids};

        Crtc* used_by_crtc = nullptr;
    };

    struct Crtc {
        struct State {
            std::vector<Plane*> using_planes;
            std::map<FrameBuffer, std::shared_ptr<uint32_t const>> fb_ids;
            DisplayMode mode = {};
        };

        uint32_t id = 0;
        std::vector<Plane*> usable_planes;
        std::map<strview, PropId*> prop_ids;
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
        std::map<strview, PropId*> prop_ids;
        PropId CRTC_ID{"CRTC_ID", &prop_ids};

        Crtc* using_crtc = nullptr;
    };

    std::shared_ptr<FileDescriptor> fd;
    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> connectors;
    std::map<uint32_t, std::string> prop_names;

    void lookup_prop_ids(uint32_t obj_id, std::map<strview, PropId*> *map) {
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

        for (auto const name_propid : *map) {
            if (name_propid.second->prop_id) continue;
            throw std::runtime_error(fmt::format(
                "DRM object #{} missing property \"{}\"",
                obj_id, name_propid.first
            ));
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

    std::shared_ptr<uint32_t const> create_fb(FrameBuffer const& fb) {
        drm_mode_fb_cmd2 fbdat = {};
        fbdat.width = fb.width;
        fbdat.height = fb.height;
        fbdat.pixel_format = fb.fourcc;
        fbdat.flags = DRM_MODE_FB_MODIFIERS;

        size_t const max_channels = std::extent_v<decltype(fbdat.handles)>;
        if (fb.channels.size() > max_channels) {
            throw std::length_error(fmt::format(
                "Too many channels for DRM framebuffer ({} > {})",
                fb.channels.size(), max_channels
            ));
        }

        std::vector<std::shared_ptr<uint32_t const>> handles;
        for (size_t c = 0; c < fb.channels.size(); ++c) {
            for (size_t pc = 0; pc < c && handles.size() <= c; ++pc) {
                if (fb.channels[c].dma_fd == fb.channels[pc].dma_fd)
                    handles.push_back(handles[pc]);
            }
            if (handles.size() <= c)
                handles.push_back(import_buffer(*fb.channels[c].dma_fd));

            fbdat.handles[c] = *handles.back();
            fbdat.pitches[c] = fb.channels[c].bytes_per_line;
            fbdat.offsets[c] = fb.channels[c].start_offset;
            fbdat.modifier[c] = fb.modifier;
        }

        fd->ioc<DRM_IOCTL_MODE_ADDFB2>(&fbdat).ex("DRM framebuffer");

        auto const fd = this->fd;
        auto const deleter = [fd](uint32_t* const id) {
            unsigned int fb_id = *id;
            delete id;
            (void) fd->ioc<DRM_IOCTL_MODE_RMFB>(&fb_id);
        };
        return {new uint32_t(fbdat.fb_id), deleter};
    }

    std::shared_ptr<uint32_t const> import_buffer(int dma_fd) {
        drm_prime_handle hdat = {.handle = 0, .flags = 0, .fd = dma_fd};
        fd->ioc<DRM_IOCTL_PRIME_FD_TO_HANDLE>(&hdat).ex("DRM buffer");

        auto const fd = this->fd;
        auto const deleter = [fd](uint32_t* const handle) {
            drm_gem_close cdat = {.handle = *handle, .pad = 0};
            delete handle;
            (void) fd->ioc<DRM_IOCTL_GEM_CLOSE>(cdat);
        };
        return {new uint32_t(hdat.handle), deleter};
    }
};

}  // anonymous namespace

//
// Driver scanning and opening
//

std::vector<DisplayDriverListing> list_display_drivers(UnixSystem* sys) {
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

        // See https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html
        drm_set_version set_ver = {1, 4, -1, -1};
        fd->ioc<DRM_IOCTL_SET_VERSION>(&set_ver).ex("Set version");

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
    UnixSystem* sys, std::string const& dev
) {
    auto driver = std::make_unique<DrmDriver>();
    driver->open(sys, dev);
    return driver;
}

//
// Structure utilities 
//

std::string debug_string(DisplayDriverListing const& d) {
    return fmt::format(
        "{} ({}): {}{}",
        d.dev_file, d.driver, d.system_path,
        d.driver_bus_id.empty() ? "" : fmt::format(" ({})", d.driver_bus_id)
    );
}

std::string debug_string(DisplayMode const& m) {
    return fmt::format(
        "{:5.1f}MHz{} {:3}[{:3}{}]{:<3} {:>4} -x- "
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