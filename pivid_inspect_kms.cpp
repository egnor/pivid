// Simple command line tool to list DRM/KMS resources.

#include <drm/drm.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include "display_output.h"
#include "unix_system.h"

namespace pivid {

bool print_properties_flag = false;  // Set by flag in main()

// Support KMS/DRM ioctl conventions for variable size arrays;
// returns true if the ioctl needs to be re-submitted with a resized array.
template <typename Pointer, typename Count, typename Item>
bool size_vec(Pointer* ptr, Count* count, std::vector<Item>* v) {
    if (*count == v->size() && *ptr == (Pointer) v->data()) return false;
    v->resize(*count);
    *ptr = (Pointer) v->data();
    return true;
}

// Scan and describe all DRM/KMS capable video cards
std::optional<DisplayDriverListing> scan_devices(std::string const& dev_arg) {
    fmt::print("=== DRM/KMS video drivers ===\n");
    std::optional<DisplayDriverListing> found;
    for (auto const& d : list_display_drivers(global_system())) {
        auto const text = debug(d);
        if (!found && text.find(dev_arg) != std::string::npos)
            found = d;
        fmt::print("{} {}\n", (found == d) ? "=>" : "  ", debug(d));
    }
    fmt::print("\n");
    return found;
}

// Fetches a KMS property blob.
std::vector<uint8_t> get_blob(
    std::unique_ptr<FileDescriptor> const& fd, uint32_t id
) {
    drm_mode_get_blob blob = {};
    blob.blob_id = id;
    std::vector<uint8_t> data;
    do {
        fd->ioc<DRM_IOCTL_MODE_GETPROPBLOB>(&blob).ex("GETPROPBLOB");
    } while (size_vec(&blob.data, &blob.length, &data));
    return data;
}

// Prints key/value properties about a KMS "object" ID,
// using the generic KMS property-value interface.
void print_properties(std::unique_ptr<FileDescriptor> const& fd, uint32_t id) {
    std::vector<uint32_t> prop_ids;
    std::vector<uint64_t> values;
    drm_mode_obj_get_properties gp = {};
    gp.obj_id = id;
    do {
        fd->ioc<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&gp).ex("OBJ_GETPROPERTIES");
    } while (
        size_vec(&gp.props_ptr, &gp.count_props, &prop_ids) +
        size_vec(&gp.prop_values_ptr, &gp.count_props, &values)
    );

    for (size_t pi = 0; pi < prop_ids.size(); ++pi) {
        std::vector<drm_mode_property_enum> enums;
        drm_mode_get_property meta = {};
        meta.prop_id = prop_ids[pi];
        do {
            meta.count_values = 0;
            fd->ioc<DRM_IOCTL_MODE_GETPROPERTY>(&meta).ex("MODE_GETPROPERTY");
        } while (size_vec(&meta.enum_blob_ptr, &meta.count_enum_blobs, &enums));

        std::string const name = meta.name;
        fmt::print("        prop #{:<3} ", prop_ids[pi]);
        if (meta.flags & DRM_MODE_PROP_IMMUTABLE) fmt::print("[ro] ");
        if (meta.flags & DRM_MODE_PROP_ATOMIC) fmt::print("[atomic] ");
        if (meta.flags & DRM_MODE_PROP_OBJECT) fmt::print("[obj] ");
        fmt::print("{} =", name);

        auto const print_fourccs = [](uint8_t const* data, int count) {
            for (int fi = 0; fi < count; ++fi) {
                if (fi % 12 == 0) fmt::print("\n           ");
                fmt::print(" ");
                for (int ci = 0; ci < 4; ++ci) {
                    int const ch = data[ci + fi * 4];
                    if (ch > 0 && ch < 32) fmt::print("{}", ch);
                    if (ch > 32) fmt::print("{:c}", ch);
                }
            }
        };

        // TODO handle RANGE and SIGNED_RANGE if we ever see any
        auto const value = values[pi];
        if (meta.flags & DRM_MODE_PROP_BITMASK) {
            fmt::print(" 0x{:x}{}", value, value ? ":" : "");
            for (auto const& en : enums) {
                if (value & (1 << en.value)) {
                    fmt::print(" {}", en.name);
                    break;
                }
            }
        } else if (!(meta.flags & DRM_MODE_PROP_BLOB)) {
            fmt::print(" {}", value);
            for (auto const& en : enums) {
                if (en.value == value) {
                    fmt::print(" ({})", en.name);
                    break;
                }
            }
        } else if (name == "MODE_ID") {
            if (value) {
                auto const blob = get_blob(fd, value);
                auto const* const mode = (drm_mode_modeinfo const*) blob.data();
                fmt::print(
                    " {}x{} {}Hz",
                    mode->hdisplay, mode->vdisplay, mode->vrefresh
                );
            } else {
                fmt::print(" (none)");
            }
        } else if (name == "IN_FORMATS") {
            auto const blob = get_blob(fd, value);
            auto const header = (drm_format_modifier_blob const*) blob.data();
            if (header->version == FORMAT_BLOB_CURRENT) {
                print_fourccs(
                    blob.data() + header->formats_offset,
                    header->count_formats
                );
            } else {
                fmt::print(" ?0x{:x}? ({}b)", header->version, blob.size());
            }
        } else if (name == "WRITEBACK_PIXEL_FORMATS") {
            auto const blob = get_blob(fd, value);
            print_fourccs(blob.data(), blob.size() / 4);
        } else {
            fmt::print(" <blob>");
        }
        fmt::print("\n");
    }
}

// Print information about the DRM/KMS resources associated with a video card.
void inspect_device(DisplayDriverListing const& listing) {
    auto const& path = listing.dev_file;
    fmt::print("=== {} ({}) ===\n", path, listing.system_path);
    fmt::print("Driver: {} v{}", listing.driver, listing.driver_date);
    if (!listing.driver_bus_id.empty())
        fmt::print(" ({})", listing.driver_bus_id);
    if (!listing.driver_desc.empty())
        fmt::print(" \"{}\"", listing.driver_desc);
    fmt::print("\n");

    auto const dev = global_system()->open(path, O_RDWR).ex(path);

    // Enable any client capabilities that expose more information.
    for (auto const& [name, cap] : {
#define C(x) std::pair<std::string_view, uint64_t>(#x, DRM_CLIENT_CAP_##x)
        C(ASPECT_RATIO),
        C(ATOMIC),
        C(STEREO_3D),
        C(UNIVERSAL_PLANES),
        C(WRITEBACK_CONNECTORS),
#undef C
    }) {
        try {
            drm_set_client_cap setcap{cap, 1};
            dev->ioc<DRM_IOCTL_SET_CLIENT_CAP>(setcap).ex(name);
        } catch (std::exception const& e) {
            fmt::print("[client cap] {}\n", e.what());
        }
    }

    drm_mode_card_res res = {};
    std::vector<uint32_t> crtc_ids, conn_ids, enc_ids;
    do {
        res.count_fbs = 0;  // Framebuffer IDs are not reported to kibitzers.
        dev->ioc<DRM_IOCTL_MODE_GETRESOURCES>(&res).ex("MODE_GETRESOURCES");
    } while (
        size_vec(&res.crtc_id_ptr, &res.count_crtcs, &crtc_ids) +
        size_vec(&res.connector_id_ptr, &res.count_connectors, &conn_ids) +
        size_vec(&res.encoder_id_ptr, &res.count_encoders, &enc_ids)
    );

    fmt::print(
        "Size: {}x{} min - {}x{} max\n",
        res.min_width, res.min_height, res.max_width, res.max_height
    );

    for (auto const& cap_name : {
#define C(X) std::pair<uint64_t, std::string>{DRM_CAP_##X, #X}
        C(DUMB_BUFFER),
        C(VBLANK_HIGH_CRTC),
        C(DUMB_PREFERRED_DEPTH),
        C(DUMB_PREFER_SHADOW),
        C(PRIME),
        C(TIMESTAMP_MONOTONIC),
        C(ASYNC_PAGE_FLIP),
        C(CURSOR_WIDTH),
        C(CURSOR_HEIGHT),
        C(ADDFB2_MODIFIERS),
        C(PAGE_FLIP_TARGET),
        C(CRTC_IN_VBLANK_EVENT),
        C(SYNCOBJ),
        C(SYNCOBJ_TIMELINE),
#undef C
    }) {
        drm_get_cap get = {cap_name.first, 0};
        if (!dev->ioc<DRM_IOCTL_GET_CAP>(&get).err)
            fmt::print("[cap] {} = {}\n", cap_name.second, get.value);
    }

    fmt::print("\n");

    //
    // Planes (framebuffers can't be inspected from another process, sadly)
    //

    drm_mode_get_plane_res planes = {};
    std::vector<uint32_t> plane_ids;
    do {
        dev->ioc<DRM_IOCTL_MODE_GETPLANERESOURCES>(&planes).ex(
            "MODE_GETPLANERESOURCES"
        );
    } while (size_vec(&planes.plane_id_ptr, &planes.count_planes, &plane_ids));

    fmt::print("{} image planes:\n", plane_ids.size());
    for (auto const plane_id : plane_ids) {
        drm_mode_get_plane plane = {};
        plane.plane_id = plane_id;
        dev->ioc<DRM_IOCTL_MODE_GETPLANE>(&plane).ex("MODE_GETPLANE");

        fmt::print("    Plane #{:<3} [CRTC", plane.plane_id);
        for (size_t ci = 0; ci < crtc_ids.size(); ++ci) {
            if (plane.possible_crtcs & (1 << ci)) {
                fmt::print(
                    " #{}{}", crtc_ids[ci],
                    crtc_ids[ci] == plane.crtc_id ? "*" : ""
                );
            }
        }
        fmt::print("]");

        if (plane.fb_id) fmt::print(" [FB #{}*]", plane.fb_id);

        std::vector<uint32_t> prop_ids;
        std::vector<uint64_t> values;
        drm_mode_obj_get_properties gp = {};
        gp.obj_id = plane.plane_id;
        gp.obj_type = DRM_MODE_OBJECT_PLANE;
        do {
            dev->ioc<DRM_IOCTL_MODE_OBJ_GETPROPERTIES>(&gp).ex("GETPROPERTIES");
        } while (
            size_vec(&gp.props_ptr, &gp.count_props, &prop_ids) +
            size_vec(&gp.prop_values_ptr, &gp.count_props, &values)
        );

        for (size_t pi = 0; pi < prop_ids.size(); ++pi) {
            std::vector<drm_mode_property_enum> enums;
            drm_mode_get_property m = {};
            m.prop_id = prop_ids[pi];
            do {
                m.count_values = 0;
                dev->ioc<DRM_IOCTL_MODE_GETPROPERTY>(&m).ex("MODE_GETPROPERTY");
            } while (size_vec(&m.enum_blob_ptr, &m.count_enum_blobs, &enums));

            if (std::string(m.name) == "type") {
                for (auto const& en : enums) 
                    if (en.value == values[pi]) fmt::print(" {}", en.name);
            }
        }

        fmt::print("\n");
        if (print_properties_flag) print_properties(dev, plane_id);
    }
    fmt::print("\n");

    //
    // CRT controllers
    //

    fmt::print("{} CRT/scanout controllers:\n", res.count_crtcs);
    for (auto const id : crtc_ids) {
        drm_mode_crtc crtc = {};
        crtc.crtc_id = id;
        dev->ioc<DRM_IOCTL_MODE_GETCRTC>(&crtc).ex("MODE_GETCRTC");

        if (crtc.fb_id != 0) {
            fmt::print(
                "  * CRTC #{:<3} [FB #{}* ({},{})]",
                id, crtc.fb_id, crtc.x, crtc.y
            );
        } else {
            fmt::print("    CRTC #{:<3}", id);
        }

        if (crtc.mode_valid) {
            fmt::print(
                " => {}x{} {}Hz",
                crtc.mode.hdisplay, crtc.mode.vdisplay, crtc.mode.vrefresh
            );
        }

        fmt::print("\n");
        if (print_properties_flag) print_properties(dev, id);
    }
    fmt::print("\n");

    //
    // Encoders
    //

    fmt::print("{} signal encoders:\n", res.count_encoders);
    for (auto const id : enc_ids) {
        drm_mode_get_encoder enc = {};
        enc.encoder_id = id;
        dev->ioc<DRM_IOCTL_MODE_GETENCODER>(&enc).ex("MODE_GETENCODER");

        fmt::print("  {} Enc #{:<3} [CRTC", enc.crtc_id != 0 ? '*' : ' ', id);
        for (size_t c = 0; c < crtc_ids.size(); ++c) {
            if (enc.possible_crtcs & (1 << c))
                fmt::print(
                    " #{}{}", crtc_ids[c],
                    crtc_ids[c] == enc.crtc_id ? "*" : ""
                );
        }
        fmt::print("]");

        switch (enc.encoder_type) {
#define E(X) case DRM_MODE_ENCODER_##X: fmt::print(" {}", #X); break
            E(NONE);
            E(DAC);
            E(TMDS);
            E(LVDS);
            E(TVDAC);
            E(VIRTUAL);
            E(DSI);
            E(DPMST);
            E(DPI);
#undef E
            default: fmt::print(" ?{}?", enc.encoder_type); break;
        }

        fmt::print("\n");
        // Note: Encoders don't have object properties.
    }
    fmt::print("\n");

    //
    // Connectors
    //

    fmt::print("{} video connectors:\n", res.count_connectors);
    for (auto const id : conn_ids) {
        std::vector<uint32_t> enc_ids;
        std::vector<drm_mode_modeinfo> modes;
        drm_mode_get_connector conn = {};
        conn.connector_id = id;
        do {
            conn.count_props = 0;
            dev->ioc<DRM_IOCTL_MODE_GETCONNECTOR>(&conn).ex("DRM conn");
        } while (
            size_vec(&conn.encoders_ptr, &conn.count_encoders, &enc_ids) +
            size_vec(&conn.modes_ptr, &conn.count_modes, &modes)
        );

        fmt::print(
            "  {} Conn #{:<3}", conn.connection == 1 ? '*' : ' ', id
        );

        fmt::print(" [Enc");
        for (auto const enc_id : enc_ids)
            fmt::print(" #{}{}", enc_id, enc_id == conn.encoder_id ? "*" : "");
        fmt::print("]");

        switch (conn.connector_type) {
#define C(X) case DRM_MODE_CONNECTOR_##X: fmt::print(" {}", #X); break
            C(Unknown);
            C(VGA);
            C(DVII);
            C(DVID);
            C(DVIA);
            C(Composite);
            C(SVIDEO);
            C(LVDS);
            C(Component);
            C(9PinDIN);
            C(DisplayPort);
            C(HDMIA);
            C(HDMIB);
            C(TV);
            C(eDP);
            C(VIRTUAL);
            C(DSI);
            C(WRITEBACK);
#undef C
            default: fmt::print(" ?{}?", conn.connector_type); break;
        }

        fmt::print("-{}", conn.connector_type_id);
        if (conn.mm_width || conn.mm_height) {
            fmt::print(" ({}x{}mm)", conn.mm_width, conn.mm_height);
        }
        fmt::print("\n");

        if (print_properties_flag) {
            print_properties(dev, id);
            for (auto const& mode : modes) {
                fmt::print(
                    "        [mode] {:>4}x{:<4} {}Hz",
                    mode.hdisplay, mode.vdisplay, mode.vrefresh
                );
                for (uint32_t bit = 1; bit; bit <<= 1) {
                    if (mode.type & bit) {
                        switch (bit) {
#define T(X) case DRM_MODE_TYPE_##X: fmt::print(" {}", #X); break
                            T(BUILTIN);
                            T(CLOCK_C);
                            T(CRTC_C);
                            T(PREFERRED);
                            T(DEFAULT);
                            T(USERDEF);
                            T(DRIVER);
#undef T
                            default: fmt::print(" ?type={}?", bit); break;
                        }
                    }
                }
                for (uint32_t bit = 1; bit; bit <<= 1) {
                    if (mode.flags & bit) {
                        switch (bit) {
#define F(X) case DRM_MODE_FLAG_##X: fmt::print(" {}", #X); break
                            F(PHSYNC);
                            F(NHSYNC);
                            F(PVSYNC);
                            F(NVSYNC);
                            F(INTERLACE);
                            F(DBLSCAN);
                            F(CSYNC);
                            F(PCSYNC);
                            F(NCSYNC);
                            F(HSKEW);
                            F(BCAST);
                            F(PIXMUX);
                            F(DBLCLK);
                            F(CLKDIV2);
                            F(3D_FRAME_PACKING);
                            F(3D_FIELD_ALTERNATIVE);
                            F(3D_LINE_ALTERNATIVE);
                            F(3D_SIDE_BY_SIDE_FULL);
                            F(3D_L_DEPTH);
                            F(3D_L_DEPTH_GFX_GFX_DEPTH);
                            F(3D_TOP_AND_BOTTOM);
                            F(3D_SIDE_BY_SIDE_HALF);
                            F(PIC_AR_4_3);
                            F(PIC_AR_16_9);
                            F(PIC_AR_64_27);
                            F(PIC_AR_256_135);
#undef F
                            default: fmt::print(" ?0x{:x}?", bit); break;
                        }
                    }
                }
                fmt::print("\n");
            }
        }
    }
    fmt::print("\n");
}

}  // namespace pivid

int main(int argc, char** argv) {
    std::string dev_arg;

    CLI::App app("Inspect kernel display (DRM/KMS) devices");
    app.add_option("--dev", dev_arg, "DRM/KMS device (in /dev/dri) to inspect");
    app.add_flag(
        "--print_properties", pivid::print_properties_flag,
        "List properties, capabilities, and modes"
    );
    CLI11_PARSE(app, argc, argv);

    try {
        auto const& listing = pivid::scan_devices(dev_arg);
        if (listing) pivid::inspect_device(*listing);
    } catch (std::exception const& e) {
        fmt::print("*** {}\n", e.what());
        return 1;
    }
    return 0;
}
