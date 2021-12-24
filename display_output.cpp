#include "display_output.h"

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <drm/drm.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <type_traits>

#include <fmt/core.h>

namespace pivid {

namespace {

//
// DRM/KMS-specific error handling
//

using strview = std::string_view;

class DrmError : public DisplayError {
  public:
    DrmError(strview action, strview note, strview error, int errcode = 0) {
        what_val = std::string{action};
        if (!note.empty()) what_val += fmt::format(" ({})", note);
        if (!error.empty()) what_val += fmt::format(": {}", error);
        if (errcode) what_val += ": " + std::system_category().message(errcode);
    }

    virtual char const* what() const noexcept { return what_val.c_str(); }

  private:
    std::string what_val;
};

int check_ret(int ret, strview action, strview note = "") {
    if (ret >= 0) return ret;  // No error.
    throw DrmError(action, note, "", errno);
}

//
// I/O helpers
//

class FileDescriptor {
  public:
    FileDescriptor() {}
    ~FileDescriptor() noexcept { if (fd >= 0) ::close(fd); }
    FileDescriptor(FileDescriptor const&) = delete;
    FileDescriptor& operator=(FileDescriptor const&) = delete;

    void init(int const fd, strview action, strview note = "") {
        assert(this->fd < 0);
        this->fd = check_ret(fd, action, note);
    }

    template <uint32_t nr>
    int io_noarg(strview action, strview note = "") {
        static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
        return check_ret(retry_ioctl(nr, nullptr), action, note);
    }

    template <uint32_t nr, typename T>
    int io_write(T const& value, strview action, strview note = "") {
        static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
        return check_ret(retry_ioctl(nr, (void*) &value), action, note);
    }

    template <uint32_t nr, typename T>
    int io(T* value, strview action, strview note = "") {
        static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
        static_assert(_IOC_SIZE(nr) == sizeof(T));
        return check_ret(retry_ioctl(nr, value), action, note);
    }

  private:
    int fd = -1;

    int retry_ioctl(uint32_t io, void* arg) {
        int ret;
        do { ret = ::ioctl(fd, io, arg); } while (ret < 0 && errno == EINTR);
        return ret;
    }
};

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
        return int8_t((drm.flags & neg) ? -1 : (drm.flags & pos) ? +1 : 0);
    };

    return {
        .horiz = {
            .clock = drm.clock, 
            .display = drm.hdisplay,
            .sync_start = drm.hsync_start,
            .sync_end = drm.hsync_end,
            .total = drm.htotal,
            .sync_polarity = sign(DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PHSYNC),
        },
        .vert = {
            .clock = drm.vrefresh, 
            .display = drm.vdisplay,
            .sync_start = drm.vsync_start,
            .sync_end = drm.vsync_end,
            .total = drm.vtotal,
            .sync_polarity = sign(DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_PVSYNC),
        },
        .pixel_skew = drm.hskew,
        .line_repeats = uint16_t(
            ((drm.flags & DRM_MODE_FLAG_DBLSCAN) ? 2 : 1)
            * (drm.vscan ? drm.vscan : 1)
        ),
        .interlace = bool(drm.flags & DRM_MODE_FLAG_INTERLACE),
        .clock_exp2 = sign(DRM_MODE_FLAG_CLKDIV2, DRM_MODE_FLAG_DBLCLK),
        .csync_polarity = sign(DRM_MODE_FLAG_NCSYNC, DRM_MODE_FLAG_PCSYNC),
        .name = drm.name,
        .preferred = bool(drm.type & DRM_MODE_TYPE_PREFERRED),
    };
}

drm_mode_modeinfo mode_to_drm(DisplayMode const& mode) {
    drm_mode_modeinfo out = {
        .clock = mode.horiz.clock,
        .hdisplay = mode.horiz.display,
        .hsync_start = mode.horiz.sync_start,
        .hsync_end = mode.horiz.sync_end,
        .htotal = mode.horiz.total,
        .hskew = mode.pixel_skew,
        .vdisplay = mode.vert.display,
        .vsync_start = mode.vert.sync_start,
        .vsync_end = mode.vert.sync_end,
        .vtotal = mode.vert.total,
        .vscan = mode.line_repeats,
        .vrefresh = mode.vert.clock,
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
        fd.init(::open(dev.c_str(), O_RDWR), "Open", dev.native());
        fd.io_noarg<DRM_IOCTL_SET_MASTER>("Set master");
        fd.io_write<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_ATOMIC, 1},
            "Set atomic capability"
        );
        fd.io_write<DRM_IOCTL_SET_CLIENT_CAP>(
            drm_set_client_cap{DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1},
            "Set universal plane capability"
        );

        drm_mode_card_res cr = {};
        std::vector<uint32_t> crtc_ids;
        std::vector<uint32_t> conn_ids;
        do {
            cr.count_fbs = cr.count_encoders = 0;  // Don't use these.
            fd.io<DRM_IOCTL_MODE_GETRESOURCES>(&cr, "Get resources");
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
                fd.io<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Connector");
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
                fd.io<DRM_IOCTL_MODE_GETENCODER>(&e, "Encoder");
                for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                    if (e.possible_crtcs & (1 << i)) {
                        auto* const crtc = &crtcs[crtc_ids[i]];
                        conn->crtcs.push_back(crtc);
                        if (c.encoder_id == enc_id && e.crtc_id == crtc->id)
                            crtc->in_use = true;
                    }
                }
            }
        }

        drm_mode_get_plane_res p = {};
        std::vector<uint32_t> plane_ids;
        do {
            fd.io<DRM_IOCTL_MODE_GETPLANERESOURCES>(&p, "Get planes");
        } while (update_vec(&p.plane_id_ptr, &p.count_planes, &plane_ids));

        for (auto const plane_id : plane_ids) {
            auto* plane = &planes[plane_id];
            plane->info.plane_id = plane_id;
            fd.io<DRM_IOCTL_MODE_GETPLANE>(&plane->info, "Get plane");
            plane->in_use = plane->info.crtc_id;
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
                    fd.io<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&g, "Props");
                } while (
                    update_vec(&g.props_ptr, &g.count_props, &prop_ids) +
                    update_vec(&g.prop_values_ptr, &g.count_props, &values)
                );

                for (auto const prop_id : prop_ids) {
                    if (checked_prop_ids.insert(prop_id).second) {
                        drm_mode_get_property p = {};
                        p.prop_id = prop_id;
                        fd.io<DRM_IOCTL_MODE_GETPROPERTY>(&p, "Prop");
                        auto const it = props.find(p.name);
                        if (it != props.end()) it->second->id = prop_id;
                    }
                }
            }
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
                fd.io<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Get connector");
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
                fd.io<DRM_IOCTL_MODE_GETENCODER>(&encoder, "Encoder");
                if (encoder.crtc_id) {
                    drm_mode_crtc crtc = {};
                    crtc.crtc_id = encoder.crtc_id;
                    fd.io<DRM_IOCTL_MODE_GETCRTC>(&crtc, "CRTC");
                    if (crtc.mode_valid)
                        output.active_mode.emplace(mode_from_drm(crtc.mode));
                }
            }

            out.push_back(std::move(output));
        }
        return out;
    }

    virtual void set_outputs(std::vector<DisplayOutputRequest> const& reqs) {
        std::vector<Plane*> old_planes;
        for (auto& id_plane : planes) {
            if (id_plane.second.in_use) old_planes.push_back(&id_plane.second);
            id_plane.second.in_use = false;
        }

        std::vector<Crtc*> old_crtcs;
        for (auto& id_crtc : crtcs) {
            if (id_crtc.second.in_use) old_crtcs.push_back(&id_crtc.second);
            id_crtc.second.in_use = false;
        }

        std::vector<Connector*> old_conns;
        for (auto& id_conn : conns) {
            if (id_conn.second.active) old_conns.push_back(&id_conn.second);
            id_conn.second.active = false;
        }

        std::map<uint32_t, std::map<uint32_t, uint64_t>> obj_props;
        for (auto const& req : reqs) {
            auto const id_conn = conns.find(req.connector_id);
            if (id_conn == conns.end()) {
                throw DrmError(
                    "Connector", fmt::format("{}", req.connector_id),
                    "Bad ID"
                );
            }

            if (!req.mode) continue;
            auto* conn = &id_conn->second;
            if (conn->active) throw DrmError("Connector", conn->name, "Reused");
            conn->active = true;

            for (auto* crtc : conn->crtcs) {
                if (crtc->in_use) continue;
                crtc->in_use = true;

                obj_props[conn->id][prop_crtc_id.id] = crtc->id;
                auto* crtc_props = &obj_props[crtc->id];
                (*crtc_props)[prop_active.id] = 1;

                auto const drm_mode = mode_to_drm(*req.mode);
                static_assert(sizeof(crtc->last_mode) == sizeof(drm_mode));
                if (memcmp(&drm_mode, &crtc->last_mode, sizeof(drm_mode))) {
                    if (crtc->mode_blob_id) {
                        drm_mode_destroy_blob d = {crtc->mode_blob_id};
                        fd.io<DRM_IOCTL_MODE_DESTROYPROPBLOB>(&d, "Del blob");
                    }

                    drm_mode_create_blob c = {};
                    c.data = (int64_t) &drm_mode;
                    c.length = sizeof(drm_mode);
                    fd.io<DRM_IOCTL_MODE_CREATEPROPBLOB>(&c, "Create blob");
                    crtc->last_mode = drm_mode;
                    crtc->mode_blob_id = c.blob_id;
                    (*crtc_props)[prop_mode_id.id] = crtc->mode_blob_id;
                }

                conn = nullptr;
                break;
            }

            if (conn) throw DrmError("Connector", conn->name, "No CRTC left");
        }

        for (auto* conn : old_conns) {
            if (!conn->active) obj_props[conn->id][prop_crtc_id.id] = 0;
        }
        for (auto* crtc : old_crtcs) {
            if (!crtc->in_use) obj_props[crtc->id][prop_active.id] = 0;
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

        fd.io<DRM_IOCTL_MODE_ATOMIC>(&atomic, "Atomic update");
    }

  private:
    struct Plane {
        drm_mode_get_plane info = {};
        bool in_use = false;
    };

    struct Crtc {
        uint32_t id;
        std::vector<Plane*> planes;
        drm_mode_modeinfo last_mode = {};
        uint32_t mode_blob_id = 0;
        bool in_use = false;
    };

    struct Connector {
        uint32_t id;
        std::string name;
        std::vector<Crtc*> crtcs;
        bool active = false;
        bool prev_active = false;
    };

    struct Prop {
        Prop(strview n, std::map<strview, Prop*>* map) { (*map)[n] = this; }
        uint32_t id = 0;
    };

    FileDescriptor fd;

    std::map<uint32_t, Plane> planes;
    std::map<uint32_t, Crtc> crtcs;
    std::map<uint32_t, Connector> conns;

    std::map<strview, Prop*> props;
    Prop prop_active{"ACTIVE", &props};
    Prop prop_crtc_id{"CRTC_ID", &props};
    Prop prop_mode_id{"MODE_ID", &props};

    std::map<std::string, uint32_t, std::less<>> prop_names;
    std::set<uint32_t> prop_ids_named;
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

    try {
        std::filesystem::path const dri_dir = "/dev/dri";
        for (auto const& entry : std::filesystem::directory_iterator(dri_dir)) {
            std::string const fname = entry.path().filename();
            if (fname.substr(0, 4) != "card" || !isdigit(fname[4])) continue;

            DisplayDriverListing listing;
            listing.dev_file = entry.path();
            auto const dev = entry.path().c_str();

            struct stat fstat;
            check_ret(stat(dev, &fstat), "Stat", dev);
            if ((fstat.st_mode & S_IFMT) != S_IFCHR)
                throw DrmError("Dev", dev, "Not a char special device");

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
    } catch (std::filesystem::filesystem_error const& e) {
        throw DrmError("File", e.path1().native(), "", e.code().value());
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
