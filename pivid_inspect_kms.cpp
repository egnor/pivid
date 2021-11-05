// Simple command line tool to list DRM/KMS resources.

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/usage.h>
#include <absl/strings/str_format.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

ABSL_FLAG(bool, print_properties, false, "Print detailed properties");

// Scan all DRM/KMS capable video cards and print a line for each.
void scan_gpus() {
    absl::PrintF("=== Scanning DRM/KMS GPU devices ===\n");
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
            absl::PrintF("*** %s: %s\n", path, strerror(errno));
            continue;
        }

        auto* const ver = drmGetVersion(fd);
        if (!ver) {
            absl::PrintF("*** Reading version (%s): %s\n", path, strerror(errno));
        } else {
            absl::PrintF("%s", path);

            // See https://www.kernel.org/doc/html/v5.10/gpu/drm-uapi.html
            drmSetVersion api_version = {1, 4, -1, -1};
            drmSetInterfaceVersion(fd, &api_version);
            auto* const busid = drmGetBusid(fd);
            if (busid) {
                if (*busid) absl::PrintF(" (%s)", busid);
                drmFreeBusid(busid);
            }

            absl::PrintF("\n    %s v%s: %s\n", ver->name, ver->date, ver->desc);
            drmFreeVersion(ver);
        }

        drmClose(fd);
    }

    if (dev_files.empty()) {
        absl::PrintF("*** No DRM/KMS devices found\n");
    } else {
        absl::PrintF(
            "--- %d DRM/KMS device(s); inspect with --dev=<dev>\n",
            dev_files.size()
        );
    }
}

// If --print_properties is set, prints key/value properties about a
// KMS "object" ID, using the generic KMS property-value interface.
void maybe_print_properties(int const fd, uint32_t const id) {
    if (!absl::GetFlag(FLAGS_print_properties)) return;

    auto* const props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_ANY);
    if (!props) return;

    for (uint32_t pi = 0; pi < props->count_props; ++pi) {
        auto* const meta = drmModeGetProperty(fd, props->props[pi]);
        if (!meta) {
            absl::PrintF("*** Prop #%d: %s\n", props->props[pi], strerror(errno));
            exit(1);
        }

        std::string const name = meta->name;
        auto const value = props->prop_values[pi];
        absl::PrintF("        Prop #%d %s =", props->props[pi], name);
        if (meta->flags & DRM_MODE_PROP_BLOB) {
            absl::PrintF(" <blob>");
        } else {
            absl::PrintF(" %d", value);
            for (int ei = 0; ei < meta->count_enums; ++ei) {
                if (meta->enums[ei].value == value) {
                    absl::PrintF(" (%s)", meta->enums[ei].name);
                    break;
                }
            }
        }
        if (meta->flags & DRM_MODE_PROP_IMMUTABLE) absl::PrintF(" [ro]");

        if (name == "IN_FORMATS" && (meta->flags & DRM_MODE_PROP_BLOB)) {
            auto* const formats = drmModeGetPropertyBlob(fd, value);
            auto const data = (char const*) formats->data;
            auto const header = (drm_format_modifier_blob const*) data;
            if (header->version == FORMAT_BLOB_CURRENT) {
                for (uint32_t fi = 0; fi < header->count_formats; ++fi) {
                    if (fi % 12 == 0) absl::PrintF("\n           ");
                    auto const* fourcc = data + header->formats_offset + fi * 4;
                    absl::PrintF(" %.4s", fourcc);
                }
            }
            drmModeFreePropertyBlob(formats);
        }

        absl::PrintF("\n");
        drmModeFreeProperty(meta);
    }

    drmModeFreeObjectProperties(props);
}

// Print information about the DRM/KMS resources associated with a video card.
void inspect_gpu(std::string const& path) {
    absl::PrintF("=== %s ===\n", path);

    int const fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        absl::PrintF("*** %s: %s\n", path, strerror(errno));
        exit(1);
    }

    // Enable any client capabilities that expose more information.
    drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);

    auto* const res = drmModeGetResources(fd);
    if (!res) {
        absl::PrintF("*** Reading resources (%s): %s\n", path, strerror(errno));
        exit(1);
    }

    auto* const ver = drmGetVersion(fd);
    if (!ver) {
        absl::PrintF("*** Reading version (%s): %s\n", path, strerror(errno));
        exit(1);
    }
    absl::PrintF(
        "Driver: %s v%s (%dx%d max)\n\n",
        ver->name, ver->date, res->max_width, res->max_height
    );

    //
    // Planes (framebuffers can't be inspected from another process, sadly)
    //

    auto* const planes = drmModeGetPlaneResources(fd);
    if (!planes) {
        absl::PrintF("*** Plane resources (%s): %s\n", path, strerror(errno));
        exit(1);
    }

    absl::PrintF("%d image planes:\n", planes->count_planes);
    for (uint32_t p = 0; p < planes->count_planes; ++p) {
        auto const id = planes->planes[p];
        auto* const plane = drmModeGetPlane(fd, id);
        if (!plane) {
            absl::PrintF("*** Plane #%d (%s): %s\n", id, path, strerror(errno));
            exit(1);
        }

        absl::PrintF("    Plane #%-3d [CRTC", id);
        for (int ci = 0; ci < res->count_crtcs; ++ci) {
            if (plane->possible_crtcs & (1 << ci))
                absl::PrintF(
                    " #%d%s", res->crtcs[ci],
                    res->crtcs[ci] == plane->crtc_id ? "*" : ""
                );
        }
        absl::PrintF("]");

        if (plane->fb_id) {
            absl::PrintF(
                " [FB #%d* (%d,%d)=>(%d,%d)]", plane->fb_id,
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
                            absl::PrintF(" %s", meta->enums[ei].name);
                    }
                }
                drmModeFreeProperty(meta);
            }
            drmModeFreeObjectProperties(props);
        }

        absl::PrintF("\n");
        maybe_print_properties(fd, id);
        drmModeFreePlane(plane);
    }
    absl::PrintF("\n");
    drmModeFreePlaneResources(planes);

    //
    // CRT controllers
    //

    absl::PrintF("%d CRT/scanout controllers:\n", res->count_crtcs);
    for (int ci = 0; ci < res->count_crtcs; ++ci) {
        auto const id = res->crtcs[ci];
        auto* const crtc = drmModeGetCrtc(fd, id);
        if (!crtc) {
            absl::PrintF("*** CRTC #%d (%s): %s\n", id, path, strerror(errno));
            exit(1);
        }

        if (crtc->buffer_id != 0) {
            absl::PrintF(
                "  * CRTC #%-3d [FB #%d* (%d,%d)+(%dx%d)]",
                id, crtc->buffer_id,
                crtc->x, crtc->y, crtc->width, crtc->height
            );
        } else {
            absl::PrintF("    CRTC #%-3d", id);
        }

        if (crtc->mode_valid) {
            absl::PrintF(
                " => %dx%d @%dHz",
                crtc->mode.hdisplay, crtc->mode.vdisplay,
                crtc->mode.vrefresh
            );
        }

        absl::PrintF("\n");
        maybe_print_properties(fd, id);
        drmModeFreeCrtc(crtc);
    }
    absl::PrintF("\n");

    //
    // Encoders
    //

    absl::PrintF("%d signal encoders:\n", res->count_encoders);
    for (int ei = 0; ei < res->count_encoders; ++ei) {
        auto const id = res->encoders[ei];
        auto* const enc = drmModeGetEncoder(fd, id);
        if (!enc) {
            absl::PrintF("*** Encoder #%d (%s): %s\n", id, path, strerror(errno));
            exit(1);
        }

        absl::PrintF(
            "  %c Enc #%-3d [CRTC", enc->crtc_id != 0 ? '*' : ' ', id
        );
        for (int c = 0; c < res->count_crtcs; ++c) {
            if (enc->possible_crtcs & (1 << c))
                absl::PrintF(
                    " #%d%s", res->crtcs[c],
                    res->crtcs[c] == enc->crtc_id ? "*" : ""
                );
        }
        absl::PrintF("]");

        switch (enc->encoder_type) {
#define E(X) case DRM_MODE_ENCODER_##X: absl::PrintF(" %s", #X); break
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
            default: absl::PrintF(" ?%d?", enc->encoder_type); break;
        }

        absl::PrintF("\n");
        maybe_print_properties(fd, id);
        drmModeFreeEncoder(enc);
    }
    absl::PrintF("\n");

    //
    // Connectors
    //

    absl::PrintF("%d video connectors:\n", res->count_connectors);
    for (int ci = 0; ci < res->count_connectors; ++ci) {
        auto const id = res->connectors[ci];
        auto* const conn = drmModeGetConnector(fd, id);
        if (!conn) {
            absl::PrintF("*** Conn #%d (%s): %s\n", id, path, strerror(errno));
            exit(1);
        }

        absl::PrintF(
            "  %c Conn #%-3d",
            conn->connection == DRM_MODE_CONNECTED ? '*' : ' ', id
        );

        absl::PrintF(" [Enc");
        for (int e = 0; e < conn->count_encoders; ++e) {
            absl::PrintF(
                " #%d%s", conn->encoders[e],
                conn->encoders[e] == conn->encoder_id ? "*" : ""
            );
        }
        absl::PrintF("]");

        switch (conn->connector_type) {
#define C(X) case DRM_MODE_CONNECTOR_##X: absl::PrintF(" %s", #X); break
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
            default: absl::PrintF(" ?%d?", conn->connector_type); break;
        }

        absl::PrintF("-%d", conn->connector_type_id);
        if (conn->mmWidth || conn->mmHeight) {
            absl::PrintF(" (%dx%dmm)", conn->mmWidth, conn->mmHeight);
        }

        absl::PrintF("\n");
        maybe_print_properties(fd, id);
        drmModeFreeConnector(conn);
    }
    absl::PrintF("\n");
    drmFreeVersion(ver);
    drmModeFreeResources(res);
    close(fd);
}

ABSL_FLAG(std::string, dev, "", "DRM/KMS device (in /dev/dri) to inspect");

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage("Inspect kernel display (DRM/KMS) devices");
    absl::ParseCommandLine(argc, argv);
    if (!absl::GetFlag(FLAGS_dev).empty()) {
        inspect_gpu(absl::GetFlag(FLAGS_dev));
    } else {
        scan_gpus();
    }
    return 0;
}
