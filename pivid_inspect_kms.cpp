// Simple command line tool to list DRM/KMS resources.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

DEFINE_bool(verbose, false, "Print detailed properties");

// Scan all DRM/KMS capable video cards and print a line for each.
void scan_gpus() {
    fmt::print("=== Scanning DRM/KMS GPU devices ===\n");
    const std::filesystem::path dri_dir = "/dev/dri";
    std::vector<std::string> dev_files;
    for (const auto &entry : std::filesystem::directory_iterator(dri_dir)) {
        const std::string filename = entry.path().filename();
        if (filename.substr(0, 4) == "card" && isdigit(filename[4]))
            dev_files.push_back(entry.path().native());
    }

    std::sort(dev_files.begin(), dev_files.end());
    for (const auto &path : dev_files) {
        const int fd = open(path.c_str(), O_RDWR);
        if (fd < 0) {
            fmt::print("*** Error opening: {}\n", path);
            continue;
        }

        auto* const ver = drmGetVersion(fd);
        if (!ver) {
            fmt::print("*** {}: Error reading version\n", path);
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

    if (!dev_files.empty()) {
        fmt::print(
            "--- {} DRM/KMS device(s); inspect with --dev=<dev>\n\n",
            dev_files.size()
        );
    } else {
        fmt::print("No cards found\n\n");
    }
}

// Print key/value properties about a KMS "object" ID,
// using the generic KMS property-value interface.
void print_gpu_object_properties(const int fd, const uint32_t id) {
    auto* const props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_ANY);
    if (!props) return;

    for (uint32_t pi = 0; pi < props->count_props; ++pi) {
        auto* const meta = drmModeGetProperty(fd, props->props[pi]);
        if (!meta) {
            fmt::print("*** Error reading property #{}\n", props->props[pi]);
            exit(1);
        }

        const std::string name = meta->name;
        const auto value = props->prop_values[pi];
        fmt::print("        Prop #{} {} =", props->props[pi], name);
        if (meta->flags & DRM_MODE_PROP_BLOB) {
            fmt::print(" <blob>");
        } else {
            fmt::print(" {}", value);
            for (int ei = 0; ei < meta->count_enums; ++ei) {
                if (meta->enums[ei].value == value) {
                    fmt::print(" ({})", meta->enums[ei].name);
                    break;
                }
            }
        }
        if (meta->flags & DRM_MODE_PROP_IMMUTABLE) fmt::print(" [ro]");

        if (name == "IN_FORMATS" && (meta->flags & DRM_MODE_PROP_BLOB)) {
            auto* const formats = drmModeGetPropertyBlob(fd, value);
            const auto data = (const char *) formats->data;
            const auto header = (const drm_format_modifier_blob *) data;
            if (header->version == FORMAT_BLOB_CURRENT) {
                for (uint32_t fi = 0; fi < header->count_formats; ++fi) {
                    if (fi % 12 == 0) fmt::print("\n           ");
                    const auto* fourcc = data + header->formats_offset + fi * 4;
                    fmt::print(" {:.4s}", fourcc);
                }
            }
            drmModeFreePropertyBlob(formats);
        }

        fmt::print("\n");
        drmModeFreeProperty(meta);
    }

    drmModeFreeObjectProperties(props);
}

// Print information about the DRM/KMS resources associated with a video card.
void inspect_gpu(const std::string& path) {
    fmt::print("=== {} ===\n", path);

    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        fmt::print("*** Error opening: {}\n", path);
        exit(1);
    }

    // Enable any client capabilities that expose more information.
    drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);

    auto* const res = drmModeGetResources(fd);
    if (!res) {
        fmt::print("*** {}: Error reading resources\n", path);
        exit(1);
    }

    auto* const ver = drmGetVersion(fd);
    if (!ver) {
        fmt::print("*** {}: Error reading version\n", path);
        exit(1);
    }
    fmt::print(
        "Driver: {} v{} ({}x{} max)\n\n",
        ver->name, ver->date, res->max_width, res->max_height
    );

    //
    // Planes (framebuffers can't be inspected from another process, sadly)
    //

    auto* const planes = drmModeGetPlaneResources(fd);
    if (!planes) {
        fmt::print("*** {}: Error reading plane resources\n", path);
        exit(1);
    }

    fmt::print("{} image planes:\n", planes->count_planes);
    for (uint32_t p = 0; p < planes->count_planes; ++p) {
        const auto id = planes->planes[p];
        auto* const plane = drmModeGetPlane(fd, id);
        if (!plane) {
            fmt::print("*** {}: Error reading plane #{}\n", path, id);
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

        const auto obj_type = DRM_MODE_OBJECT_PLANE;
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
        if (FLAGS_verbose) print_gpu_object_properties(fd, id);
        drmModeFreePlane(plane);
    }
    fmt::print("\n");
    drmModeFreePlaneResources(planes);

    //
    // CRT controllers
    //

    fmt::print("{} CRT/scanout controllers:\n", res->count_crtcs);
    for (int ci = 0; ci < res->count_crtcs; ++ci) {
        const auto id = res->crtcs[ci];
        auto* const crtc = drmModeGetCrtc(fd, id);
        if (!crtc) {
            fmt::print("*** {}: Error reading CRTC #{}\n", path, id);
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
        if (FLAGS_verbose) print_gpu_object_properties(fd, id);
        drmModeFreeCrtc(crtc);
    }
    fmt::print("\n");

    //
    // Encoders
    //

    fmt::print("{} signal encoders:\n", res->count_encoders);
    for (int ei = 0; ei < res->count_encoders; ++ei) {
        const auto id = res->encoders[ei];
        auto* const enc = drmModeGetEncoder(fd, id);
        if (!enc) {
            fmt::print("*** {}: Error reading encoder #{}\n", path, id);
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
        if (FLAGS_verbose) print_gpu_object_properties(fd, id);
        drmModeFreeEncoder(enc);
    }
    fmt::print("\n");

    //
    // Connectors
    //

    fmt::print("{} video connectors:\n", res->count_connectors);
    for (int ci = 0; ci < res->count_connectors; ++ci) {
        const auto id = res->connectors[ci];
        auto* const conn = drmModeGetConnector(fd, id);
        if (!conn) {
            fmt::print("*** {}: Error reading connector #{}\n", path, id);
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
        if (FLAGS_verbose) print_gpu_object_properties(fd, id);
        drmModeFreeConnector(conn);
    }
    fmt::print("\n");
    drmFreeVersion(ver);
    drmModeFreeResources(res);
    close(fd);
}

DEFINE_string(dev, "", "DRM/KMS device (in /dev/dri) to inspect");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (!FLAGS_dev.empty()) {
        inspect_gpu(FLAGS_dev);
    } else {
        scan_gpus();
    }
    return 0;
}
