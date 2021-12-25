#include "display_output.h"

#undef NDEBUG
#include <assert.h>
#include <drm/drm.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <fmt/core.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>
#include <type_traits>

#include "file_descriptor.h"

namespace pivid {

namespace {

using strview = std::string_view;

//
// Helpers for KMS/DRM interface
//

// Support KMS/DRM ioctl conventions for variable size arrays;
// returns true if the ioctl needs to be re-submitted with a resized array.
template <typename Pointer, typename Count, typename Item>
bool update_vec(Pointer* ptr, Count* count, std::vector<Item>* v) {
    if (*count == v->size() && *ptr == (Pointer) v->data()) return false;
    v->resize(*count);
    *ptr = (Pointer) v->data();
    return true;
}

DisplayMode mode_from_drm(drm_mode_modeinfo const& drm) {
    auto const sign = [&drm](uint32_t neg, uint32_t pos) -> int8_t {
        return (drm.flags & neg) ? -1 : (drm.flags & pos) ? +1 : 0;
    };

    return {
        .horiz = {
            .clock = int(drm.clock), 
            .display = drm.hdisplay,
            .sync_start = drm.hsync_start,
            .sync_end = drm.hsync_end,
            .total = drm.htotal,
            .sync_polarity = sign(DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PHSYNC),
        },
        .vert = {
            .clock = int(drm.vrefresh), 
            .display = drm.vdisplay,
            .sync_start = drm.vsync_start,
            .sync_end = drm.vsync_end,
            .total = drm.vtotal,
            .sync_polarity = sign(DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_PVSYNC),
        },
        .pixel_skew = drm.hskew,
        .line_repeats =
            ((drm.flags & DRM_MODE_FLAG_DBLSCAN) ? 2 : 1)
            * (drm.vscan ? drm.vscan : 1),
        .interlace = bool(drm.flags & DRM_MODE_FLAG_INTERLACE),
        .clock_exp2 = sign(DRM_MODE_FLAG_CLKDIV2, DRM_MODE_FLAG_DBLCLK),
        .csync_polarity = sign(DRM_MODE_FLAG_NCSYNC, DRM_MODE_FLAG_PCSYNC),
        .name = drm.name,
        .preferred = bool(drm.type & DRM_MODE_TYPE_PREFERRED),
    };
}

drm_mode_modeinfo mode_to_drm(DisplayMode const& mode) {
    drm_mode_modeinfo out = {
        .clock = uint32_t(mode.horiz.clock),
        .hdisplay = uint16_t(mode.horiz.display),
        .hsync_start = uint16_t(mode.horiz.sync_start),
        .hsync_end = uint16_t(mode.horiz.sync_end),
        .htotal = uint16_t(mode.horiz.total),
        .hskew = uint16_t(mode.pixel_skew),
        .vdisplay = uint16_t(mode.vert.display),
        .vsync_start = uint16_t(mode.vert.sync_start),
        .vsync_end = uint16_t(mode.vert.sync_end),
        .vtotal = uint16_t(mode.vert.total),
        .vscan = uint16_t(mode.line_repeats),
        .vrefresh = uint32_t(mode.vert.clock),
        .flags = uint32_t(
            (mode.horiz.sync_polarity > 0 ? DRM_MODE_FLAG_PHSYNC : 0) |
            (mode.horiz.sync_polarity < 0 ? DRM_MODE_FLAG_NHSYNC : 0) |
            (mode.vert.sync_polarity > 0 ? DRM_MODE_FLAG_PVSYNC : 0) |
            (mode.vert.sync_polarity < 0 ? DRM_MODE_FLAG_NVSYNC : 0) |
            (mode.interlace ? DRM_MODE_FLAG_INTERLACE : 0) |
            (mode.csync_polarity ? DRM_MODE_FLAG_CSYNC : 0) |
            (mode.csync_polarity > 0 ? DRM_MODE_FLAG_PCSYNC : 0) |
            (mode.csync_polarity < 0 ? DRM_MODE_FLAG_NCSYNC : 0) |
            (mode.pixel_skew ? DRM_MODE_FLAG_HSKEW : 0) |
            (mode.clock_exp2 > 0 ? DRM_MODE_FLAG_DBLCLK : 0) |
            (mode.clock_exp2 < 0 ? DRM_MODE_FLAG_CLKDIV2 : 0)
        ),
        .type = uint32_t(
            DRM_MODE_TYPE_USERDEF |
            (mode.preferred ? DRM_MODE_TYPE_PREFERRED : 0)
        ),
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
    void open(std::filesystem::path const& dev) {
        fd = std::make_shared<FileDescriptor>();
        fd->init(::open(dev.c_str(), O_RDWR), "Open", dev.native());
        fd->io<DRM_IOCTL_SET_MASTER>("Set master");
        fd->io<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_ATOMIC, 1},
            "Set atomic capability"
        );
        fd->io<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1},
            "Set universal plane capability"
        );

        drm_mode_card_res cr = {};
        std::vector<uint32_t> crtc_ids;
        std::vector<uint32_t> conn_ids;
        do {
            cr.count_fbs = cr.count_encoders = 0;  // Don't use these.
            fd->io<DRM_IOCTL_MODE_GETRESOURCES>(&cr, "Get resources");
        } while (
            update_vec(&cr.crtc_id_ptr, &cr.count_crtcs, &crtc_ids) +
            update_vec(&cr.connector_id_ptr, &cr.count_connectors, &conn_ids)
        );

        for (auto const crtc_id : crtc_ids) crtcs[crtc_id].id = crtc_id;

        for (auto const conn_id : conn_ids) {
            drm_mode_get_connector c = {};
            c.connector_id = conn_id;
            std::vector<uint32_t> enc_ids;
            do {
                c.count_props = c.count_modes = 0;
                fd->io<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Connector");
            } while (update_vec(&c.encoders_ptr, &c.count_encoders, &enc_ids));

            auto* conn = &conns[conn_id];
            conn->id = conn_id;
            switch (c.connector_type) {
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
                default: conn->name = fmt::format("[#{}]", c.connector_type);
            }
            conn->name += fmt::format("-{}", c.connector_type_id);

            for (auto const enc_id : enc_ids) {
                drm_mode_get_encoder e = {};
                e.encoder_id = enc_id;
                fd->io<DRM_IOCTL_MODE_GETENCODER>(&e, "Encoder");
                for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                    if (e.possible_crtcs & (1 << i)) {
                        auto* const crtc = &crtcs[crtc_ids[i]];
                        conn->crtcs.push_back(crtc);
                        if (c.encoder_id == enc_id && e.crtc_id == crtc->id)
                            crtc->state.in_use = true;
                    }
                }
            }
        }

        drm_mode_get_plane_res p = {};
        std::vector<uint32_t> plane_ids;
        do {
            fd->io<DRM_IOCTL_MODE_GETPLANERESOURCES>(&p, "Get planes");
        } while (update_vec(&p.plane_id_ptr, &p.count_planes, &plane_ids));

        for (auto const plane_id : plane_ids) {
            auto* plane = &planes[plane_id];
            plane->info.plane_id = plane_id;
            fd->io<DRM_IOCTL_MODE_GETPLANE>(&plane->info, "Get plane");
            plane->state.in_use = plane->info.crtc_id;
            for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                if (plane->info.possible_crtcs & (1 << i))
                    crtcs[crtc_ids[i]].planes.push_back(plane);
            }
        }

        std::set<uint32_t> checked_prop_ids;
        for (auto const* id_vec : {&conn_ids, &crtc_ids, &plane_ids}) {
            for (auto const obj_id : *id_vec) {
                std::vector<uint32_t> prop_ids;
                std::vector<uint64_t> values;
                drm_mode_obj_get_properties g = {};
                g.obj_id = obj_id;
                do {
                    fd->io<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&g, "Props");
                } while (
                    update_vec(&g.props_ptr, &g.count_props, &prop_ids) +
                    update_vec(&g.prop_values_ptr, &g.count_props, &values)
                );

                for (auto const prop_id : prop_ids) {
                    if (checked_prop_ids.insert(prop_id).second) {
                        drm_mode_get_property p = {};
                        p.prop_id = prop_id;
                        fd->io<DRM_IOCTL_MODE_GETPROPERTY>(&p, "Prop");
                        auto const it = props.find(p.name);
                        if (it != props.end()) it->second->prop_id = prop_id;
                    }
                }
            }
        }

        for (auto const name_prop : props) {
            if (name_prop.second->prop_id) continue;
            throw std::runtime_error(
                fmt::format("Missing DRM property: {}", name_prop.first)
            );
        }
    }

    virtual std::vector<DisplayOutputStatus> scan_outputs() {
        std::vector<DisplayOutputStatus> out;
        for (auto const& id_conn : conns) {
            drm_mode_get_connector c = {};
            c.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                c.count_props = c.count_encoders = 0;
                fd->io<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Get connector");
            } while (update_vec(&c.modes_ptr, &c.count_modes, &modes));

            DisplayOutputStatus output = {};
            output.connector_id = id_conn.first;
            output.name = id_conn.second.name;
            if (c.connection < 3) output.connected.emplace(c.connection == 1); 

            for (auto const& mode : modes) {
                if (!(mode.flags & DRM_MODE_FLAG_3D_MASK))
                    output.modes.push_back(mode_from_drm(mode));
            }

            if (c.encoder_id) {
                drm_mode_get_encoder encoder = {};
                encoder.encoder_id = c.encoder_id;
                fd->io<DRM_IOCTL_MODE_GETENCODER>(&encoder, "Encoder");
                if (encoder.crtc_id) {
                    drm_mode_crtc crtc = {};
                    crtc.crtc_id = encoder.crtc_id;
                    fd->io<DRM_IOCTL_MODE_GETCRTC>(&crtc, "CRTC");
                    if (crtc.mode_valid)
                        output.active_mode.emplace(mode_from_drm(crtc.mode));
                }
            }

            out.push_back(std::move(output));
        }
        return out;
    }

    virtual void make_updates(std::vector<DisplayOutputUpdate> const& updates) {
        for (auto blob_id : blobs_to_free) {
            drm_mode_destroy_blob d = {blob_id};
            fd->io<DRM_IOCTL_MODE_DESTROYPROPBLOB>(&d, "Release blob");
        }
        blobs_to_free.clear();

        for (auto& id_p : planes) id_p.second.pending = {};
        for (auto& id_c : crtcs) id_c.second.pending = {};
        for (auto& id_c : conns) id_c.second.pending = {};

        std::map<uint32_t, std::map<uint32_t, uint64_t>> obj_props;
        for (auto const& upd : updates) {
            if (!upd.mode) continue;

            auto const id_conn = conns.find(upd.connector_id);
            if (id_conn == conns.end()) {
                throw std::invalid_argument(
                    fmt::format("Bad connector ID: {}", upd.connector_id)
                );
            }

            auto* conn = &id_conn->second;
            if (conn->pending.active)
                throw std::invalid_argument("Double setup: " + conn->name);
            conn->pending.active = true;

            Crtc* crtc = nullptr;
            for (auto* c : conn->crtcs) {
                if (c->pending.in_use) continue;
                c->pending.in_use = true;
                crtc = c;
                break;
            }
            if (!crtc) throw std::runtime_error("No CRTC found: " + conn->name);

            obj_props[conn->id][CRTC_ID.prop_id] = crtc->id;
            auto* crtc_props = &obj_props[crtc->id];
            (*crtc_props)[ACTIVE.prop_id] = 1;

            auto const drm_mode = mode_to_drm(*upd.mode);
            static_assert(sizeof(crtc->state.mode) == sizeof(drm_mode));
            if (memcmp(&drm_mode, &crtc->state.mode, sizeof(drm_mode))) {
                drm_mode_create_blob c = {};
                c.data = (int64_t) &drm_mode;
                c.length = sizeof(drm_mode);
                fd->io<DRM_IOCTL_MODE_CREATEPROPBLOB>(&c, "Blob");
                blobs_to_free.push_back(c.blob_id);
                (*crtc_props)[MODE_ID.prop_id] = c.blob_id;
                crtc->pending.mode = drm_mode;
            }

            // go through layers
            // - find an unused plane for each (mark used)
            // - compare FrameBuffer, do nothing if same
            // - convert each dma_fd into drm buffer handle, using cache
            // - create drm framebuffer
            // - add drm buffer handles & drm framebuffer to CRTC retain
        }

        for (auto& id_conn : conns) {
            if (id_conn.second.state.active && !id_conn.second.pending.active)
                obj_props[id_conn.first][CRTC_ID.prop_id] = 0;
        }

        for (auto& id_crtc : crtcs) {
            if (id_crtc.second.state.in_use && !id_crtc.second.pending.in_use)
                obj_props[id_crtc.first][ACTIVE.prop_id] = 0;
        }

        std::vector<uint32_t> obj_ids;
        std::vector<uint32_t> obj_prop_counts;
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> prop_values;
        for (auto const& obj_prop : obj_props) {
            obj_ids.push_back(obj_prop.first);
            obj_prop_counts.push_back(obj_prop.second.size());
            for (auto const& prop_value : obj_prop.second) {
               prop_ids.push_back(prop_value.first);
               prop_values.push_back(prop_value.second);
            }
        }

        drm_mode_atomic atomic = {
            .flags = DRM_MODE_ATOMIC_ALLOW_MODESET,
            .count_objs = obj_ids.size(),
            .objs_ptr = (uint64_t) obj_ids.data(),
            .count_props_ptr = (uint64_t) obj_prop_counts.data(),
            .props_ptr = (uint64_t) prop_ids.data(),
            .prop_values_ptr = (uint64_t) prop_values.data(),
            .reserved = 0,
            .user_data = 0,
        };

        fd->io<DRM_IOCTL_MODE_ATOMIC>(&atomic, "Atomic update");

        for (auto& id_p : planes) id_p.second.state = id_p.second.pending;
        for (auto& id_c : crtcs) id_c.second.state = id_c.second.pending;
        for (auto& id_c : conns) id_c.second.state = id_c.second.pending;
    }

  private:
    struct Plane {
        struct State {
            bool in_use = false;
            FrameBuffer buffer = {};
            std::vector<std::shared_ptr<int const>> retain;
        };

        drm_mode_get_plane info = {};
        State state, pending;
    };

    struct Crtc {
        struct State {
            bool in_use = false;
            drm_mode_modeinfo mode = {};
        };

        uint32_t id = 0;
        std::vector<Plane*> planes;
        std::map<uint64_t, std::vector<std::shared_ptr<int const>>> retain;
        State state, pending;
    };

    struct Connector {
        struct State {
            bool active = false;
        };

        uint32_t id = 0;
        std::string name;
        std::vector<Crtc*> crtcs;
        State state, pending;
    };

    struct PropId {
        PropId(strview n, std::map<strview, PropId*>* map) { (*map)[n] = this; }
        uint32_t prop_id = 0;  // Filled in init() as props are discovered.
    };

    std::shared_ptr<FileDescriptor> fd;

    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> conns;

    using CacheMap = std::map<
        std::weak_ptr<int const>,
        std::shared_ptr<int const>,
        std::owner_less<>
    >;
    CacheMap cache_dma_drm;
    CacheMap::iterator cache_clean_iter;

    std::map<strview, PropId*> props;
#define PROP_ID(X) PropId X{#X, &props}
    PROP_ID(ACTIVE);
    PROP_ID(CRTC_H);
    PROP_ID(CRTC_ID);
    PROP_ID(CRTC_W);
    PROP_ID(CRTC_X);
    PROP_ID(CRTC_Y);
    PROP_ID(FB_ID);
    PROP_ID(MODE_ID);
    PROP_ID(SRC_H);
    PROP_ID(SRC_W);
    PROP_ID(SRC_X);
    PROP_ID(SRC_Y);
#undef P

    std::vector<uint32_t> blobs_to_free;
};

}  // anonymous namespace

//
// Driver initialization
//

std::unique_ptr<DisplayDriver> open_display_driver(
    std::filesystem::path const& dev
) {
    auto driver = std::make_unique<DrmDriver>();
    driver->open(dev);
    return driver;
}

std::vector<DisplayDriverListing> list_display_drivers() {
    std::vector<DisplayDriverListing> out;

    std::filesystem::path const dri_dir = "/dev/dri";
    for (auto const& entry : std::filesystem::directory_iterator(dri_dir)) {
        std::string const fname = entry.path().filename();
        if (fname.substr(0, 4) != "card" || !isdigit(fname[4])) continue;

        DisplayDriverListing listing;
        listing.dev_file = entry.path();
        auto const dev = entry.path().c_str();

        struct stat fstat;
        check_sys(stat(dev, &fstat), "Stat", dev);
        if ((fstat.st_mode & S_IFMT) != S_IFCHR) {
            throw std::system_error(
                std::make_error_code(std::errc::no_such_device),
                entry.path().native()
            );
        }

        auto const maj = major(fstat.st_rdev), min = minor(fstat.st_rdev);
        auto const dev_link = fmt::format("/sys/dev/char/{}:{}", maj, min);
        auto const device = std::filesystem::canonical(dev_link);
        auto const path = std::filesystem::relative(device, "/sys/devices");
        listing.system_path = path.native();

        FileDescriptor fd;
        fd.init(::open(dev, O_RDWR), "Open", dev);

        // See https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html
        drm_set_version set_ver = {1, 4, -1, -1};
        fd.io<DRM_IOCTL_SET_VERSION>(&set_ver, "Set version");

        std::vector<char> name, date, desc;
        drm_version ver = {};
        do {
            fd.io<DRM_IOCTL_VERSION>(&ver, "Get version");
        } while (
            update_vec(&ver.name, &ver.name_len, &name) +
            update_vec(&ver.date, &ver.date_len, &date) +
            update_vec(&ver.desc, &ver.desc_len, &desc)
        );
        listing.driver.assign(name.begin(), name.end());
        listing.driver_date.assign(date.begin(), date.end());
        listing.driver_desc.assign(desc.begin(), desc.end());

        std::vector<char> bus_id;
        drm_unique uniq = {};
        do {
            fd.io<DRM_IOCTL_GET_UNIQUE>(&uniq, "Get unique");
        } while (update_vec(&uniq.unique, &uniq.unique_len, &bus_id));
        listing.driver_bus_id.assign(bus_id.begin(), bus_id.end());
        out.push_back(std::move(listing));
    }

    return out;
}

//
// Structure utilities 
//

std::string DisplayMode::format() const {
    return fmt::format(
        "H:{:5.1f}MHz{} {:4}{} {:3}[{:3}{}]{:<3}  "
        "V:{:2}Hz {:4}{}{} {:2}[{:2}{}]{:<2}{} \"{}\"{}",
        horiz.clock / 1024.0,
        clock_exp2 > 0 ? "*" : clock_exp2 < 0 ? "/" : " ",
        horiz.display, pixel_skew ? fmt::format(" >{}", pixel_skew) : "",
        horiz.sync_start - horiz.display,
        horiz.sync_end - horiz.sync_start,
        horiz.sync_polarity < 0 ? "-" : horiz.sync_polarity > 0 ? "+" : "",
        horiz.total - horiz.sync_end,
        vert.clock, vert.display, interlace ? "i" : "p",
        line_repeats > 1 ? fmt::format("*{}", line_repeats) : "",
        vert.sync_start - vert.display,
        vert.sync_end - vert.sync_start,
        vert.sync_polarity < 0 ? "-" : vert.sync_polarity > 0 ? "+" : "",
        vert.total - vert.sync_end,
        csync_polarity < 0 ? " C-" : csync_polarity > 0 ? " C+" : "",
        name, preferred ? " best" : ""
    );
}

}  // namespace pivid
