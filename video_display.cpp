#include "video_display.h"

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

    template <uint32_t io>
    int io_noarg(strview action, strview note = "") {
        static_assert(_IOC_DIR(io) == _IOC_NONE && _IOC_SIZE(io) == 0);
        return check_ret(retry_ioctl(io, nullptr), action, note);
    }

    template <uint32_t io, typename T>
    T io_read(strview action, strview note = "") {
        static_assert(_IOC_DIR(io) == _IOC_READ && _IOC_SIZE(io) == sizeof(T));
        T value;
        check_ret(retry_ioctl(io, &value), action, note);
        return value;
    }

    template <uint32_t io, typename T>
    int io_write(T const& value, strview action, strview note = "") {
        static_assert(_IOC_DIR(io) == _IOC_WRITE && _IOC_SIZE(io) == sizeof(T));
        return check_ret(retry_ioctl(io, (void*) &value), action, note);
    }

    template <uint32_t io, typename T>
    int io_update(T* value, strview action, strview note = "") {
        static_assert(_IOC_DIR(io) == (_IOC_READ | _IOC_WRITE));
        static_assert(_IOC_SIZE(io) == sizeof(T));
        return check_ret(retry_ioctl(io, value), action, note);
    }

  private:
    int fd = -1;

    int retry_ioctl(uint32_t io, void* arg) {
        int ret;
        do { ret = ::ioctl(fd, io, arg); } while (ret < 0 && errno == EINTR);
        return ret;
    }
};

//
// DisplayDriver implementation
//

// Supports DRM conventions for variable size array returns:
// Returns true if the ioctl needs to be (re-)issued to fetch data.
template <typename Pointer, typename Count, typename Item>
bool update_vec(Pointer* ptr, Count* count, std::vector<Item>* v) {
    if (*count == v->size() && *ptr == (Pointer) v->data()) return false;
    v->resize(*count);
    *ptr = (Pointer) v->data();
    return true;
}

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
            fd.io_update<DRM_IOCTL_MODE_GETRESOURCES>(&cr, "Get resources");
        } while (
            update_vec(&cr.crtc_id_ptr, &cr.count_crtcs, &crtc_ids) +
            update_vec(&cr.connector_id_ptr, &cr.count_connectors, &conn_ids)
        );

        for (auto const conn_id : conn_ids) {
            drm_mode_get_connector c = {};
            c.connector_id = conn_id;
            std::vector<uint32_t> enc_ids;
            do {
                c.count_props = c.count_modes = 0;
                fd.io_update<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Connector");
            } while (update_vec(&c.encoders_ptr, &c.count_encoders, &enc_ids));

            auto* crtc_set = &connector_crtcs[conn_id];
            for (auto const enc_id : enc_ids) {
                drm_mode_get_encoder encoder = {};
                encoder.encoder_id = enc_id;
                fd.io_update<DRM_IOCTL_MODE_GETENCODER>(&encoder, "Encoder");
                for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                    if (encoder.possible_crtcs & (1 << i))
                        crtc_set->insert(crtc_ids[i]);
                }
            }
        }

        drm_mode_get_plane_res p = {};
        std::vector<uint32_t> plane_ids;
        do {
            fd.io_update<DRM_IOCTL_MODE_GETPLANERESOURCES>(&p, "Get planes");
        } while (update_vec(&p.plane_id_ptr, &p.count_planes, &plane_ids));

        for (auto const plane_id : plane_ids) {
            drm_mode_get_plane plane = {};
            plane.plane_id = plane_id;
            fd.io_update<DRM_IOCTL_MODE_GETPLANE>(&plane, "Get plane");
            planes[plane_id] = plane;
            for (unsigned i = 0; i < crtc_ids.size(); ++i) {
                if (plane.possible_crtcs & (1 << i))
                    crtc_planes[crtc_ids[i]].insert(plane_id);
            }
        }

        for (auto const conn_id : conn_ids) obj_props(conn_id);
        for (auto const crtc_id : crtc_ids) obj_props(crtc_id);
        for (auto const plane_id : plane_ids) obj_props(plane_id);
    }

    virtual std::vector<DisplayConnectorListing> list_connectors() {
        std::vector<DisplayConnectorListing> out;
        for (auto const id_conn : connector_crtcs) {
            drm_mode_get_connector c = {};
            c.connector_id = id_conn.first;
            std::vector<drm_mode_modeinfo> modes;
            do {
                c.count_props = c.count_encoders = 0;
                fd.io_update<DRM_IOCTL_MODE_GETCONNECTOR>(&c, "Get connector");
            } while (update_vec(&c.modes_ptr, &c.count_modes, &modes));

            DisplayConnectorListing listing = {};
            listing.id = c.connector_id;
            listing.which = c.connector_type_id;
            if (c.connection < 3) listing.connected.emplace(c.connection == 1); 
            switch (c.connector_type) {
#define T(x) case DRM_MODE_CONNECTOR_##x: listing.type = #x; break
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
                default: listing.type = fmt::format("?{}?", c.connector_type);
            }

            for (auto const& mode : modes) {
                if (!(mode.flags & DRM_MODE_FLAG_3D_MASK))
                    listing.modes.push_back(mode_from_drm(mode));
            }

            if (c.encoder_id) {
                drm_mode_get_encoder encoder = {};
                encoder.encoder_id = c.encoder_id;
                fd.io_update<DRM_IOCTL_MODE_GETENCODER>(&encoder, "Encoder");
                if (encoder.crtc_id) {
                    drm_mode_crtc crtc = {};
                    crtc.crtc_id = encoder.crtc_id;
                    fd.io_update<DRM_IOCTL_MODE_GETCRTC>(&crtc, "CRTC");
                    if (crtc.mode_valid)
                        listing.active_mode.emplace(mode_from_drm(crtc.mode));
                }
            }

            out.push_back(std::move(listing));
        }
        return out;
    }

  private:
    FileDescriptor fd;

    std::map<uint32_t, std::set<uint32_t>> connector_crtcs;
    std::map<uint32_t, std::set<uint32_t>> crtc_planes;
    std::map<uint32_t, drm_mode_get_plane> planes;

    std::map<std::string, uint32_t, std::less<>> prop_names;
    std::set<uint32_t> prop_ids_named;

    std::map<uint32_t, uint64_t> obj_props(uint32_t obj_id) {
        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> prop_values;
        drm_mode_obj_get_properties g = {};
        g.obj_id = obj_id;
        do {
            fd.io_update<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&g, "Properties");
        } while (
            update_vec(&g.props_ptr, &g.count_props, &prop_ids) +
            update_vec(&g.prop_values_ptr, &g.count_props, &prop_values)
        );

        std::map<uint32_t, uint64_t> out;
        for (unsigned i = 0; i < prop_ids.size(); ++i) {
            out[prop_ids[i]] = prop_values[i];
            if (!prop_ids_named.count(prop_ids[i])) {
                drm_mode_get_property p = {};
                p.prop_id = prop_ids[i];
                fd.io_update<DRM_IOCTL_MODE_GETPROPERTY>(&p, "Get property");
                prop_names[p.name] = p.prop_id;
            }
        }
        return out;
    }

    static DisplayMode mode_from_drm(drm_mode_modeinfo const& drm) {
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
                .sync = sign(DRM_MODE_FLAG_NHSYNC, DRM_MODE_FLAG_PHSYNC),
            },
            .vert = {
                .clock = drm.vrefresh, 
                .display = drm.vdisplay,
                .sync_start = drm.vsync_start,
                .sync_end = drm.vsync_end,
                .total = drm.vtotal,
                .sync = sign(DRM_MODE_FLAG_NVSYNC, DRM_MODE_FLAG_PVSYNC),
            },
            .pixel_skew = drm.hskew,
            .line_reps = drm.vdisplay,
            .interlace = bool(drm.flags & DRM_MODE_FLAG_INTERLACE),
            .clock_exp2 = sign(DRM_MODE_FLAG_CLKDIV2, DRM_MODE_FLAG_DBLCLK),
            .composite_sync = sign(DRM_MODE_FLAG_NCSYNC, DRM_MODE_FLAG_PCSYNC),
            .name = drm.name,
            .preferred = bool(drm.type & DRM_MODE_TYPE_PREFERRED),
        };
    }
};

}  // anonymous namespace

std::unique_ptr<DisplayDriver> open_display_driver(
    std::filesystem::path const& dev
) {
    auto driver = std::make_unique<DrmDriver>();
    driver->open(dev);
    return driver;
}

//
// Driver initialization
//

std::vector<DisplayDriverListing> list_drivers() {
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
            fd.io_update<DRM_IOCTL_SET_VERSION>(&set_ver, "Set version");

            std::vector<char> name, date, desc;
            drm_version ver = {};
            do {
                fd.io_update<DRM_IOCTL_VERSION>(&ver, "Get version");
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
                fd.io_update<DRM_IOCTL_GET_UNIQUE>(&uniq, "Get unique");
            } while (update_vec(&uniq.unique, &uniq.unique_len, &bus_id));
            listing.driver_bus_id.assign(bus_id.begin(), bus_id.end());

            out.push_back(std::move(listing));
        }
    } catch (std::filesystem::filesystem_error const& e) {
        throw DrmError("File", e.path1().native(), "", e.code().value());
    }

    return out;
}

}  // namespace pivid
