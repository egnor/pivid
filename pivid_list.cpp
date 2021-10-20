#include <fcntl.h>

#include <vector>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/strings/str_format.h>

ABSL_FLAG(int, card, -1, "Video card number (/dev/dri/card#) to inspect");
ABSL_FLAG(bool, props, false, "Print detailed object properties");

void scan_cards() {
    absl::PrintF("=== Scanning video cards ===\n");
    int found = 0;
    for (int card = 0; card < DRM_MAX_MINOR; ++card) {
        const auto path = absl::StrFormat("/dev/dri/card%d", card);
        const int fd = open(path.c_str(), O_RDWR);
        if (fd >= 0) {
            auto* const ver = drmGetVersion(fd);
            if (ver == nullptr) {
                absl::PrintF("*** %s: Error reading version\n", path);
            } else {
                // See https://www.kernel.org/doc/html/v4.18/gpu/drm-uapi.html
                drmSetVersion sv = {1, 4, -1, -1};
                drmSetInterfaceVersion(fd, &sv);
                auto* const busid = drmGetBusid(fd);
                if (busid == nullptr) {
                    absl::PrintF("*** %s: Error reading bus ID\n", path);
                } else {
                    ++found;
                    absl::PrintF("card #%d", card);
                    if (*busid) absl::PrintF(" (%s)", busid);
                    absl::PrintF(
                        ": %s (%s) %s\n", ver->name, ver->date, ver->desc
                    );
                    drmFreeBusid(busid);
                }
                drmFreeVersion(ver);
            }
            drmClose(fd);
        }
    }

    if (found) {
        absl::PrintF(
            "%d card(s) found; inspect one with: pivid_list --card=#\n\n", found
        );
    } else {
        absl::PrintF("No cards found\n\n");
    }
}

void print_properties(const int fd, const uint32_t id) {
    auto* const props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_ANY);
    if (props == nullptr) return;

    for (uint32_t p = 0; p < props->count_props; ++p) {
        const auto value = props->prop_values[p];
        auto* const meta = drmModeGetProperty(fd, props->props[p]);
        if (meta == nullptr) {
            absl::PrintF("*** Error reading property #%d\n", props->props[p]);
            exit(1);
        }

        absl::PrintF("        #%d %s =", props->props[p], meta->name);
        if (drm_property_type_is(meta, DRM_MODE_PROP_BLOB)) {
            absl::PrintF(" [blob]");
        } else {
            absl::PrintF(" %d", value);
            if (drm_property_type_is(meta, DRM_MODE_PROP_ENUM)) {
                for (int e = 0; e < meta->count_enums; ++e) {
                    if (meta->enums[e].value == value) {
                        absl::PrintF(" (%s)", meta->enums[e].name);
                        break;
                    }
                }
            }
        }
        if (meta->flags & DRM_MODE_PROP_IMMUTABLE) absl::PrintF(" [ro]");
        absl::PrintF("\n");
        drmModeFreeProperty(meta);
    }

    drmModeFreeObjectProperties(props);
}

void inspect_card(const int card) {
    const auto path = absl::StrFormat("/dev/dri/card%d", card);
    const bool print_props = absl::GetFlag(FLAGS_props);

    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        absl::PrintF("*** %s: Error opening\n", path);
        exit(1);
    }

    drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);

    auto* const res = drmModeGetResources(fd);
    if (res == nullptr) {
        absl::PrintF("*** %s: Error reading resources\n", path);
        exit(1);
    }

    absl::PrintF(
        "=== Inspecting card: %s (max %dx%d) ===\n", path,
        res->max_width, res->max_height
    );

    //
    // Planes (framebuffers can't be inspected, sadly)
    //

    auto* const planes = drmModeGetPlaneResources(fd);
    if (planes == nullptr) {
        absl::PrintF("*** %s: Error reading plane resources\n", path);
        exit(1);
    }

    absl::PrintF("%d image planes:\n", planes->count_planes);
    for (uint32_t p = 0; p < planes->count_planes; ++p) {
        const auto id = planes->planes[p];
        auto* const plane = drmModeGetPlane(fd, id);
        if (plane == nullptr) {
            absl::PrintF("*** %s: Error reading plane #%d\n", path, id);
            exit(1);
        }

        absl::PrintF("    Plane #%-3d [CRTC", id);
        for (int c = 0; c < res->count_crtcs; ++c) {
            if (plane->possible_crtcs & (1 << c))
                absl::PrintF(
                    " #%d%s", res->crtcs[c],
                    res->crtcs[c] == plane->crtc_id ? "*" : ""
                );
        }
        absl::PrintF("]");

        if (plane->fb_id) {
            absl::PrintF(
                " [FB #%d* (%d,%d)=>(%d,%d)]", plane->fb_id,
                plane->x, plane->y, plane->crtc_x, plane->crtc_y
           );
        }

        auto* const props = drmModeObjectGetProperties(
            fd, id, DRM_MODE_OBJECT_PLANE
        );
        if (props != nullptr) {
            for (uint32_t p = 0; p < props->count_props; ++p) {
                const auto value = props->prop_values[p];
                auto* const meta = drmModeGetProperty(fd, props->props[p]);
                if (meta == nullptr) continue;
                if (
                    !strcmp(meta->name, "type") &&
                    drm_property_type_is(meta, DRM_MODE_PROP_ENUM)
                ) {
                    for (int e = 0; e < meta->count_enums; ++e) {
                        if (meta->enums[e].value == value)
                            absl::PrintF(" %s", meta->enums[e].name);
                    }
                }
                drmModeFreeProperty(meta);
            }

            drmModeFreeObjectProperties(props);
        }

        absl::PrintF("\n");
        if (print_props) print_properties(fd, id);
        drmModeFreePlane(plane);
    }
    absl::PrintF("\n");
    drmModeFreePlaneResources(planes);

    //
    // CRTCs
    //

    absl::PrintF("%d CRT (sic) controllers:\n", res->count_crtcs);
    for (int c = 0; c < res->count_crtcs; ++c) {
        const auto id = res->crtcs[c];
        auto* const crtc = drmModeGetCrtc(fd, id);
        if (crtc == nullptr) {
            absl::PrintF("*** %s: Error reading CRTC #%d\n", path, id);
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
        if (print_props) print_properties(fd, id);
        drmModeFreeCrtc(crtc);
    }
    absl::PrintF("\n");

    //
    // Encoders
    //

    absl::PrintF("%d signal encoders:\n", res->count_encoders);
    for (int e = 0; e < res->count_encoders; ++e) {
        const auto id = res->encoders[e];
        auto* const enc = drmModeGetEncoder(fd, id);
        if (enc == nullptr) {
            absl::PrintF("*** %s: Error reading encoder #%d\n", path, id);
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
            default: absl::PrintF(" ?type?"); break;
        }

        absl::PrintF("\n");
        if (print_props) print_properties(fd, id);
        drmModeFreeEncoder(enc);
    }
    absl::PrintF("\n");

    //
    // Connectors
    //

    absl::PrintF("%d video connectors:\n", res->count_connectors);
    for (int c = 0; c < res->count_connectors; ++c) {
        const auto id = res->connectors[c];
        auto* const conn = drmModeGetConnector(fd, id);
        if (conn == nullptr) {
            absl::PrintF("*** %s: Error reading connector #%d\n", path, id);
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
            C(DPI);
            C(WRITEBACK);
            C(SPI);
#undef C
            default: absl::PrintF(" ?type?"); break;
        }

        absl::PrintF("-%d", conn->connector_type_id);
        if (conn->mmWidth || conn->mmHeight) {
            absl::PrintF(" (%dx%dmm)", conn->mmWidth, conn->mmHeight);
        }

        absl::PrintF("\n");
        if (print_props) print_properties(fd, id);
        drmModeFreeConnector(conn);
    }
    absl::PrintF("\n");
    drmModeFreeResources(res);
    close(fd);
}

int main(const int argc, char** const argv) {
    absl::ParseCommandLine(argc, argv);

    const int card = absl::GetFlag(FLAGS_card);
    if (card < 0) {
	scan_cards();
    } else {
        inspect_card(card);
    }

    return 0;
}
