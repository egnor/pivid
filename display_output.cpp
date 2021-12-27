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
    void open(std::filesystem::path const& dev) {
        fd = std::make_shared<FileDescriptor>();
        fd->init(::open(dev.c_str(), O_RDWR), "Open", dev.native());
        fd->io<DRM_IOCTL_SET_MASTER>("Become DRM master");
        fd->io<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_ATOMIC, 1},
            "Enable DRM atomic modesetting"
        );
        fd->io<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1},
            "Enable DRM universal planes"
        );

        drm_mode_card_res res = {};
        std::vector<uint32_t> crtc_ids;
        std::vector<uint32_t> conn_ids;
        do {
            res.count_fbs = res.count_encoders = 0;  // Don't use these.
            fd->io<DRM_IOCTL_MODE_GETRESOURCES>(&res, "Get DRM resources");
        } while (
            size_vec(&res.crtc_id_ptr, &res.count_crtcs, &crtc_ids) +
            size_vec(&res.connector_id_ptr, &res.count_connectors, &conn_ids)
        );

        for (auto const crtc_id : crtc_ids)
            crtcs[crtc_id].id = crtc_id;

        for (auto const conn_id : conn_ids) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = conn_id;
            std::vector<uint32_t> enc_ids;
            do {
                cdat.count_props = cdat.count_modes = 0;
                fd->io<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat, "Get DRM connector");
            } while (
                size_vec(&cdat.encoders_ptr, &cdat.count_encoders, &enc_ids)
            );

            auto* conn = &connectors[conn_id];
            conn->id = conn_id;
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
                fd->io<DRM_IOCTL_MODE_GETENCODER>(&edat, "Get DRM encoder");
                for (unsigned i = 0; i < crtc_ids.size(); ++i) {
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
            fd->io<DRM_IOCTL_MODE_GETPLANERESOURCES>(&pres, "Scan DRM planes");
        } while (size_vec(&pres.plane_id_ptr, &pres.count_planes, &plane_ids));

        for (auto const plane_id : plane_ids) {
            drm_mode_get_plane pdat = {};
            pdat.plane_id = plane_id;
            fd->io<DRM_IOCTL_MODE_GETPLANE>(&pdat, "Get DRM plane");

            auto* plane = &planes[plane_id];
            plane->id = plane_id;
            for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                if (pdat.possible_crtcs & (1 << i))
                    crtcs[crtc_ids[i]].usable_planes.push_back(plane);
            }
        }

        std::set<uint32_t> checked_prop_ids;
        for (auto const* id_vec : {&conn_ids, &crtc_ids, &plane_ids}) {
            for (auto const obj_id : *id_vec) {
                std::vector<uint32_t> prop_ids;
                std::vector<uint64_t> values;
                drm_mode_obj_get_properties odat = {};
                odat.obj_id = obj_id;
                do {
                    fd->io<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&odat, "Object");
                } while (
                    size_vec(&odat.props_ptr, &odat.count_props, &prop_ids) +
                    size_vec(&odat.prop_values_ptr, &odat.count_props, &values)
                );

                for (auto const prop_id : prop_ids) {
                    if (checked_prop_ids.insert(prop_id).second) {
                        drm_mode_get_property pdat = {};
                        pdat.prop_id = prop_id;
                        fd->io<DRM_IOCTL_MODE_GETPROPERTY>(&pdat, "Property");
                        auto const it = props.find(pdat.name);
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

    virtual std::vector<DisplayStatus> scan_outputs() {
        std::vector<DisplayStatus> out;
        for (auto const& id_conn : connectors) {
            drm_mode_get_connector cdat = {};
            cdat.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                cdat.count_props = cdat.count_encoders = 0;
                fd->io<DRM_IOCTL_MODE_GETCONNECTOR>(&cdat, "Get DRM connector");
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
                fd->io<DRM_IOCTL_MODE_GETENCODER>(&edat, "Get DRM encoder");
                if (edat.crtc_id) {
                    drm_mode_crtc ccdat = {};
                    ccdat.crtc_id = edat.crtc_id;
                    fd->io<DRM_IOCTL_MODE_GETCRTC>(&ccdat, "Get DRM CRTC");
                    if (ccdat.mode_valid)
                        status.active_mode = mode_from_drm(ccdat.mode);
                }
            }

            out.push_back(std::move(status));
        }
        return out;
    }

    virtual bool update_ready(uint32_t connector_id) {
        auto const id_conn_iter = connectors.find(connector_id);
        if (id_conn_iter == connectors.end())
            throw std::invalid_argument("Bad connector ID");

        auto const& conn = id_conn_iter->second;
        return (!conn.claimed_crtc || !conn.claimed_crtc->pending_flip);
    }

    virtual void update_output(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) {
        for (auto blob_id : blobs_to_free) {
            drm_mode_destroy_blob dblob = {blob_id};
            fd->io<DRM_IOCTL_MODE_DESTROYPROPBLOB>(&dblob, "Release DRM blob");
        }
        blobs_to_free.clear();

        auto const id_conn_iter = connectors.find(connector_id);
        if (id_conn_iter == connectors.end())
            throw std::invalid_argument("Bad connector ID");

        auto* const conn = &id_conn_iter->second;
        auto* crtc = conn->claimed_crtc;
        if (crtc) {
            if (crtc->pending_flip)
                throw std::invalid_argument("Set while pending: " + conn->name);
        } else {
            if (!mode.refresh_hz) return;  // Was off, still off
            for (auto* const try_crtc : conn->usable_crtcs) {
                if (!try_crtc->active.connector_id && !try_crtc->pending_flip) {
                    crtc = try_crtc;
                    break;
                }
            }
            if (!crtc) throw std::runtime_error("No CRTC ready: " + conn->name);
        }

        // Build the atomic update and the state that will result.
        std::map<uint32_t, std::map<uint32_t, uint64_t>> obj_props;
        Crtc::State next = {};

        if (!mode.refresh_hz) {
            obj_props[conn->id][CRTC_ID.prop_id] = 0;
            obj_props[crtc->id][ACTIVE.prop_id] = 0;
            // Leave next state zeroed.
        } else {
            if (!conn->claimed_crtc) {
                obj_props[conn->id][CRTC_ID.prop_id] = crtc->id;
                obj_props[crtc->id][ACTIVE.prop_id] = 1;
            }

            next.connector_id = connector_id;
            next.mode = mode_to_drm(mode);
            static_assert(sizeof(crtc->active.mode) == sizeof(next.mode));
            if (memcmp(&crtc->active.mode, &next.mode, sizeof(next.mode))) {
                drm_mode_create_blob cblob = {};
                cblob.data = (int64_t) &next.mode;
                cblob.length = sizeof(next.mode);
                fd->io<DRM_IOCTL_MODE_CREATEPROPBLOB>(&cblob, "Blob");
                blobs_to_free.push_back(cblob.blob_id);
                obj_props[crtc->id][MODE_ID.prop_id] = cblob.blob_id;
            }

            // TODO update layers
            (void) layers;
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
        crtc->pending_flip.emplace(std::move(next));
        conn->claimed_crtc = crtc;
    }

    // TODO after flip confirmed:
    // - if connector_id => zero, reset connector's claimed_crtc
    // - set active to pending_flip, reset pending_flip to empty

  private:
    struct Plane {
        uint32_t id = 0;
        bool claimed = false;
    };

    struct Crtc {
        struct State {
            std::vector<std::shared_ptr<void>> retain;
            std::set<Plane*> active_planes;
            drm_mode_modeinfo mode = {};
            int32_t connector_id = 0;
        };

        uint32_t id = 0;
        std::vector<Plane*> usable_planes;
        State active;
        std::optional<State> pending_flip;
    };

    struct Connector {
        uint32_t id = 0;
        std::string name;
        std::vector<Crtc*> usable_crtcs;
        Crtc* claimed_crtc = nullptr;
    };

    struct PropId {
        PropId(strview n, std::map<strview, PropId*>* map) { (*map)[n] = this; }
        uint32_t prop_id = 0;  // Filled in init() as props are discovered.
    };

    std::shared_ptr<FileDescriptor> fd;

    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> connectors;

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
            fd.io<DRM_IOCTL_GET_UNIQUE>(&uniq, "Get unique");
        } while (size_vec(&uniq.unique, &uniq.unique_len, &bus_id));
        listing.driver_bus_id.assign(bus_id.begin(), bus_id.end());
        out.push_back(std::move(listing));
    }

    return out;
}

//
// Structure utilities 
//

std::string debug_string(DisplayDriverListing const& d) {
    return fmt::format(
        "{} ({}): {}{}",
        d.dev_file.native(), d.driver, d.system_path,
        d.driver_bus_id.empty() ? "" : fmt::format(" ({})", d.driver_bus_id)
    );
}

std::string debug_string(DisplayMode const& m) {
    return fmt::format(
        "H:{:5.1f}MHz{} {:4} {:3}[{:3}{}]{:<3}  "
        "V:{:2}Hz {:4}{} {:2}[{:2}{}]{:<2} \"{}\"",
        m.pixel_khz / 1024.0,
        m.horiz.doubling > 0 ? "*2" : m.horiz.doubling < 0 ? "/2" : "  ",
        m.horiz.display,
        m.horiz.sync_start - m.horiz.display,
        m.horiz.sync_end - m.horiz.sync_start,
        m.horiz.sync_polarity < 0 ? "-" : m.horiz.sync_polarity > 0 ? "+" : "",
        m.horiz.total - m.horiz.sync_end,
        m.refresh_hz,
        m.vert.display,
        m.vert.doubling > 0 ? "p*2" : m.vert.doubling < 0 ? "i  " : "   ",
        m.vert.sync_start - m.vert.display,
        m.vert.sync_end - m.vert.sync_start,
        m.vert.sync_polarity < 0 ? "-" : m.vert.sync_polarity > 0 ? "+" : " ",
        m.vert.total - m.vert.sync_end,
        m.name
    );
}

}  // namespace pivid
