// Simple command line tool to list DRM/KMS resources.

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

bool verbose_flag = false;  // Set by flag in main()

// Scan all DRM/KMS capable video cards and print a line for each.
void scan_gpus() {
    fmt::print("=== Scanning DRM/KMS GPU devices ===\n");
    std::filesystem::path const dri_dir = "/dev/dri";
    std::vector<std::string> dev_files;
    for (auto const& entry : std::filesystem::directory_iterator(dri_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 4) == "card" && isdigit(filename[4]))
            dev_files.push_back(entry.path().native());
    }

    std::sort(dev_files.begin(), dev_files.end());
    for (auto const& path : dev_files) {
        int const fd = open(path.c_str(), O_RDWR);
        if (fd < 0) {
            fmt::print("*** {}: {}\n", path, strerror(errno));
            continue;
        }

        auto* const ver = drmGetVersion(fd);
        if (!ver) {
            fmt::print("*** Reading version ({}): {}\n", path, strerror(errno));
        } else {
            fmt::print("{}", path);

            // See https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html
            drmSetVersion api_version = {1, 4, -1, -1};
            drmSetInterfaceVersion(fd, &api_version);
            auto* const busid = drmGetBusid(fd);
            if (busid) {
                if (*busid) fmt::print(" ({})", busid);
                drmFreeBusid(busid);
            }

            fmt::print("\n    {} v{}: {}\n", ver->name, ver->date, ver->desc);
            drmFreeVersion(ver);
        }

        drmClose(fd);
    }

    if (dev_files.empty()) {
        fmt::print("*** No DRM/KMS devices found\n");
    } else {
        fmt::print(
            "--- {} DRM/KMS device(s); inspect with --dev=<dev>\n",
            dev_files.size()
        );
    }
}

// Prints key/value properties about a KMS "object" ID,
// using the generic KMS property-value interface.
void print_properties(int const fd, uint32_t const id) {
    auto* const props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_ANY);
    if (!props) return;

    for (uint32_t pi = 0; pi < props->count_props; ++pi) {
        auto* const meta = drmModeGetProperty(fd, props->props[pi]);
        if (!meta) {
            fmt::print("*** Prop #{}: {}\n", props->props[pi], strerror(errno));
            exit(1);
        }

        std::string const name = meta->name;
        fmt::print("        ");
        if (meta->flags & DRM_MODE_PROP_IMMUTABLE) fmt::print("[ro] ");
        fmt::print("{} =", name);

        auto const value = props->prop_values[pi];
        if (!(meta->flags & DRM_MODE_PROP_BLOB)) {
            fmt::print(" {}", value);
            for (int ei = 0; ei < meta->count_enums; ++ei) {
                if (meta->enums[ei].value == value) {
                    fmt::print(" ({})", meta->enums[ei].name);
                    break;
                }
            }
        } else if (name == "MODE_ID") {
            auto* const blob = drmModeGetPropertyBlob(fd, value);
            if (blob) {
                auto const* const mode = (drm_mode_modeinfo const*) blob->data;
                fmt::print(
                    " {}x{} @{}Hz",
                    mode->hdisplay, mode->vdisplay, mode->vrefresh
                );
                drmModeFreePropertyBlob(blob);
            } else {
                fmt::print(" <none>");
            }
        } else if (name == "IN_FORMATS") {
            auto* const blob = drmModeGetPropertyBlob(fd, value);
            auto const data = (char const*) blob->data;
            auto const header = (drm_format_modifier_blob const*) data;
            if (header->version == FORMAT_BLOB_CURRENT) {
                for (uint32_t fi = 0; fi < header->count_formats; ++fi) {
                    if (fi % 12 == 0) fmt::print("\n           ");
                    auto const* fourcc = data + header->formats_offset + fi * 4;
                    fmt::print(" {:.4s}", fourcc);
                }
            }
            drmModeFreePropertyBlob(blob);
        } else {
            fmt::print(" <blob>");
        }
        fmt::print("\n");
        drmModeFreeProperty(meta);
    }

    drmModeFreeObjectProperties(props);
}

// Print information about the DRM/KMS resources associated with a video card.
void inspect_gpu(std::string const& path) {
    fmt::print("=== {} ===\n", path);

    int const fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        fmt::print("*** {}: {}\n", path, strerror(errno));
        exit(1);
    }

    // Enable any client capabilities that expose more information.
    drmSetClientCap(fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    auto* const res = drmModeGetResources(fd);
    if (!res) {
        fmt::print("*** Reading resources ({}): {}\n", path, strerror(errno));
        exit(1);
    }

    auto* const ver = drmGetVersion(fd);
    if (!ver) {
        fmt::print("*** Reading version ({}): {}\n", path, strerror(errno));
        exit(1);
    }
    fmt::print(
        "Driver: {} v{} ({}x{} max)\n",
        ver->name, ver->date, res->max_width, res->max_height
    );
    if (verbose_flag) {
        uint64_t v = 0;
#define C(X) \
        if (!drmGetCap(fd, DRM_CAP_##X, &v)) \
            fmt::print("    {} = {}\n", #X, v)
        C(DUMB_BUFFER);
        C(VBLANK_HIGH_CRTC);
        C(DUMB_PREFERRED_DEPTH);
        C(DUMB_PREFER_SHADOW);
        C(PRIME);
        C(TIMESTAMP_MONOTONIC);
        C(ASYNC_PAGE_FLIP);
        C(CURSOR_WIDTH);
        C(CURSOR_HEIGHT);
        C(ADDFB2_MODIFIERS);
        C(PAGE_FLIP_TARGET);
        C(CRTC_IN_VBLANK_EVENT);
        C(SYNCOBJ);
        C(SYNCOBJ_TIMELINE);
#undef C
    }
    fmt::print("\n");

    //
    // Planes (framebuffers can't be inspected from another process, sadly)
    //

    auto* const planes = drmModeGetPlaneResources(fd);
    if (!planes) {
        fmt::print("*** Plane resources ({}): {}\n", path, strerror(errno));
        exit(1);
    }

    fmt::print("{} image planes:\n", planes->count_planes);
    for (uint32_t p = 0; p < planes->count_planes; ++p) {
        auto const id = planes->planes[p];
        auto* const plane = drmModeGetPlane(fd, id);
        if (!plane) {
            fmt::print("*** Plane #{} ({}): {}\n", id, path, strerror(errno));
            exit(1);
        }

        fmt::print("    Plane #{:<3} [CRTC", id);
        for (int ci = 0; ci < res->count_crtcs; ++ci) {
            if (plane->possible_crtcs & (1 << ci))
                fmt::print(
                    " #{}{}", res->crtcs[ci],
                    res->crtcs[ci] == plane->crtc_id ? "*" : ""
                );
        }
        fmt::print("]");

        if (plane->fb_id) {
            fmt::print(
                " [FB #{}* ({},{})=>({},{})]", plane->fb_id,
                plane->x, plane->y, plane->crtc_x, plane->crtc_y
           );
        }

        auto const obj_type = DRM_MODE_OBJECT_PLANE;
        auto* const props = drmModeObjectGetProperties(fd, id, obj_type);
        if (props) {
            for (uint32_t pi = 0; pi < props->count_props; ++pi) {
                auto* const meta = drmModeGetProperty(fd, props->props[pi]);
                if (!meta) continue;
                if (std::string(meta->name) == "type") {
                    for (int ei = 0; ei < meta->count_enums; ++ei) {
                        if (meta->enums[ei].value == props->prop_values[pi])
                            fmt::print(" {}", meta->enums[ei].name);
                    }
                }
                drmModeFreeProperty(meta);
            }
            drmModeFreeObjectProperties(props);
        }

        fmt::print("\n");
        if (verbose_flag) print_properties(fd, id);
        drmModeFreePlane(plane);
    }
    fmt::print("\n");
    drmModeFreePlaneResources(planes);

    //
    // CRT controllers
    //

    fmt::print("{} CRT/scanout controllers:\n", res->count_crtcs);
    for (int ci = 0; ci < res->count_crtcs; ++ci) {
        auto const id = res->crtcs[ci];
        auto* const crtc = drmModeGetCrtc(fd, id);
        if (!crtc) {
            fmt::print("*** CRTC #{} ({}): {}\n", id, path, strerror(errno));
            exit(1);
        }

        if (crtc->buffer_id != 0) {
            fmt::print(
                "  * CRTC #{:<3} [FB #{}* ({},{})+({}x{})]",
                id, crtc->buffer_id,
                crtc->x, crtc->y, crtc->width, crtc->height
            );
        } else {
            fmt::print("    CRTC #{:<3}", id);
        }

        if (crtc->mode_valid) {
            fmt::print(
                " => {}x{} @{}Hz",
                crtc->mode.hdisplay, crtc->mode.vdisplay,
                crtc->mode.vrefresh
            );
        }

        fmt::print("\n");
        if (verbose_flag) print_properties(fd, id);
        drmModeFreeCrtc(crtc);
    }
    fmt::print("\n");

    //
    // Encoders
    //

    fmt::print("{} signal encoders:\n", res->count_encoders);
    for (int ei = 0; ei < res->count_encoders; ++ei) {
        auto const id = res->encoders[ei];
        auto* const enc = drmModeGetEncoder(fd, id);
        if (!enc) {
            fmt::print("*** Encoder #{} ({}): {}\n", id, path, strerror(errno));
            exit(1);
        }

        fmt::print(
            "  {} Enc #{:<3} [CRTC", enc->crtc_id != 0 ? '*' : ' ', id
        );
        for (int c = 0; c < res->count_crtcs; ++c) {
            if (enc->possible_crtcs & (1 << c))
                fmt::print(
                    " #{}{}", res->crtcs[c],
                    res->crtcs[c] == enc->crtc_id ? "*" : ""
                );
        }
        fmt::print("]");

        switch (enc->encoder_type) {
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
            default: fmt::print(" ?{}?", enc->encoder_type); break;
        }

        fmt::print("\n");
        if (verbose_flag) print_properties(fd, id);
        drmModeFreeEncoder(enc);
    }
    fmt::print("\n");

    //
    // Connectors
    //

    fmt::print("{} video connectors:\n", res->count_connectors);
    for (int ci = 0; ci < res->count_connectors; ++ci) {
        auto const id = res->connectors[ci];
        auto* const conn = drmModeGetConnector(fd, id);
        if (!conn) {
            fmt::print("*** Conn #{} ({}): {}\n", id, path, strerror(errno));
            exit(1);
        }

        fmt::print(
            "  {} Conn #{:<3}",
            conn->connection == DRM_MODE_CONNECTED ? '*' : ' ', id
        );

        fmt::print(" [Enc");
        for (int e = 0; e < conn->count_encoders; ++e) {
            fmt::print(
                " #{}{}", conn->encoders[e],
                conn->encoders[e] == conn->encoder_id ? "*" : ""
            );
        }
        fmt::print("]");

        switch (conn->connector_type) {
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
            default: fmt::print(" ?{}?", conn->connector_type); break;
        }

        fmt::print("-{}", conn->connector_type_id);
        if (conn->mmWidth || conn->mmHeight) {
            fmt::print(" ({}x{}mm)", conn->mmWidth, conn->mmHeight);
        }
        fmt::print("\n");

        if (verbose_flag) {
            print_properties(fd, id);
            for (int mi = 0; mi < conn->count_modes; ++mi) {
                auto const& mode = conn->modes[mi];
                fmt::print(
                    "        [mode] {:>4}x{:<4} @{}Hz",
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

        drmModeFreeConnector(conn);
    }
    fmt::print("\n");
    drmFreeVersion(ver);
    drmModeFreeResources(res);
    close(fd);
}

int main(int argc, char** argv) {
    std::string dev;

    CLI::App app("Inspect kernel display (DRM/KMS) devices");
    app.add_option("--dev", dev, "DRM/KMS device (in /dev/dri) to inspect");
    app.add_flag("--verbose", verbose_flag, "Print properties and modes");
    CLI11_PARSE(app, argc, argv);

    if (!dev.empty()) {
        inspect_gpu(dev);
    } else {
        scan_gpus();
    }
    return 0;
}
