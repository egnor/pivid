// Simple command line tool to list DRM/KMS resources and their IDs.

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <filesystem>
#include <vector>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/strings/str_format.h>
#include <libv4l2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

ABSL_FLAG(std::string, gpu, "", "GPU device (in /dev/dri) to inspect");
ABSL_FLAG(std::string, video, "", "Video device (in /dev/v4l) to inspect");
ABSL_FLAG(bool, props, false, "Print detailed object properties");

//
// GPU listing
//

// Scan all DRM/KMS capable video cards and print a line for each.
void scan_gpus() {
    absl::PrintF("=== Scanning GPUs ===\n");
    int found = 0;
    const std::filesystem::path dri_dir = "/dev/dri/by-path";
    for (const auto &entry : std::filesystem::directory_iterator(dri_dir)) {
        const std::string filename = entry.path().filename();
        if (filename.substr(filename.size() - 5) != "-card") continue;

        const int fd = open(entry.path().c_str(), O_RDWR);
        if (fd < 0) {
            absl::PrintF("*** Error opening: %s\n", entry.path());
            continue;
        }

        auto* const ver = drmGetVersion(fd);
        if (!ver) {
            absl::PrintF("*** %s: Error reading version\n", entry.path());
        } else {
            ++found;
            absl::PrintF(
                "%s\n    %s v%s: %s\n",
                entry.path(), ver->name, ver->date, ver->desc
            );
            drmFreeVersion(ver);
        }
        drmClose(fd);
    }

    if (found) {
        absl::PrintF(
            "--- %d GPU(s); inspect with: pivid_list --gpu=<dev>\n\n",
            found
        );
    } else {
        absl::PrintF("No cards found\n\n");
    }
}

// Print key/value properties about a KMS "object" ID,
// using the generic KMS property-value interface.
void print_properties(const int fd, const uint32_t id) {
    auto* const props = drmModeObjectGetProperties(fd, id, DRM_MODE_OBJECT_ANY);
    if (!props) return;

    for (uint32_t pi = 0; pi < props->count_props; ++pi) {
        const auto value = props->prop_values[pi];
        auto* const meta = drmModeGetProperty(fd, props->props[pi]);
        if (!meta) {
            absl::PrintF("*** Error reading property #%d\n", props->props[pi]);
            exit(1);
        }

        absl::PrintF("        Prop #%d %s =", props->props[pi], meta->name);
        if (meta->flags & DRM_MODE_PROP_BLOB) {
            absl::PrintF(" [blob]");
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
        absl::PrintF("\n");
        drmModeFreeProperty(meta);
    }

    drmModeFreeObjectProperties(props);
}

// Print information about the DRM/KMS resources associated with a video card.
void inspect_gpu() {
    const auto path = absl::GetFlag(FLAGS_gpu);
    absl::PrintF("=== %s ===\n", path);

    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        absl::PrintF("*** Error opening: %s\n", path);
        exit(1);
    }

    // Enable any client capabilities that expose more information.
    drmSetClientCap(fd, DRM_CLIENT_CAP_STEREO_3D, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ASPECT_RATIO, 1);

    auto* const res = drmModeGetResources(fd);
    if (!res) {
        absl::PrintF("*** %s: Error reading resources\n", path);
        exit(1);
    }

    auto* const ver = drmGetVersion(fd);
    if (!ver) {
        absl::PrintF("*** %s: Error reading version\n", path);
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
        absl::PrintF("*** %s: Error reading plane resources\n", path);
        exit(1);
    }

    const bool print_props = absl::GetFlag(FLAGS_props);
    absl::PrintF("%d image planes:\n", planes->count_planes);
    for (uint32_t p = 0; p < planes->count_planes; ++p) {
        const auto id = planes->planes[p];
        auto* const plane = drmModeGetPlane(fd, id);
        if (!plane) {
            absl::PrintF("*** %s: Error reading plane #%d\n", path, id);
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

        drmModePropertyBlobPtr formats = nullptr;
        const auto obj_type = DRM_MODE_OBJECT_PLANE;
        auto* const props = drmModeObjectGetProperties(fd, id, obj_type);
        for (uint32_t pi = 0; props && pi < props->count_props; ++pi) {
            auto* const meta = drmModeGetProperty(fd, props->props[pi]);
            if (!meta) continue;

            const std::string name(meta->name);
            const auto value = props->prop_values[pi];
            if (name == "IN_FORMATS" && (meta->flags & DRM_MODE_PROP_BLOB)) {
                formats = drmModeGetPropertyBlob(fd, value);
            } else if (name == "type") {
                for (int ei = 0; ei < meta->count_enums; ++ei) {
                    if (meta->enums[ei].value == value)
                        absl::PrintF(" %s", meta->enums[ei].name);
                }
            }
            drmModeFreeProperty(meta);
        }
        if (props) drmModeFreeObjectProperties(props);

        if (formats && formats->length >= sizeof(drm_format_modifier_blob)) {
            const auto data = (const char *) formats->data;
            const auto header = (const drm_format_modifier_blob *) data;
            const auto arr = data + header->formats_offset;
            if (header->version == FORMAT_BLOB_CURRENT) {
                absl::PrintF("\n        Formats:");
                for (uint32_t fi = 0; fi < header->count_formats; ++fi) {
                    if (fi && (fi % 12) == 11) absl::PrintF("\n           ");
                    const auto* fourcc = data + header->formats_offset + fi * 4;
                    absl::PrintF(" %.4s", fourcc);
                }
            }
            drmModeFreePropertyBlob(formats);
        }

        absl::PrintF("\n");
        if (print_props) print_properties(fd, id);
        drmModeFreePlane(plane);
    }
    absl::PrintF("\n");
    drmModeFreePlaneResources(planes);

    //
    // CRT controllers
    //

    absl::PrintF("%d CRT (sic) controllers:\n", res->count_crtcs);
    for (int ci = 0; ci < res->count_crtcs; ++ci) {
        const auto id = res->crtcs[ci];
        auto* const crtc = drmModeGetCrtc(fd, id);
        if (!crtc) {
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
    for (int ei = 0; ei < res->count_encoders; ++ei) {
        const auto id = res->encoders[ei];
        auto* const enc = drmModeGetEncoder(fd, id);
        if (!enc) {
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
            default: absl::PrintF(" ?%d?", enc->encoder_type); break;
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
    for (int ci = 0; ci < res->count_connectors; ++ci) {
        const auto id = res->connectors[ci];
        auto* const conn = drmModeGetConnector(fd, id);
        if (!conn) {
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
            default: absl::PrintF(" ?%d?", conn->connector_type); break;
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
    drmFreeVersion(ver);
    drmModeFreeResources(res);
    close(fd);
}

//
// Video device listing
//

// Print capability bits from VIDIOC_QUERYCAP results.
void print_capability(const int fd) {
    v4l2_capability cap = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        absl::PrintF("*** Error querying device\n");
        exit(1);
    }

    const uint32_t v = cap.version;
    absl::PrintF(
        "%s v%d.%d.%d:",
        (const char *) cap.driver, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    );

    const auto caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        if (!(caps & bit)) continue;
        switch (bit) {
#define C(X) case V4L2_CAP_##X: absl::PrintF(" %s", #X); break
            C(VIDEO_CAPTURE);
            C(VIDEO_CAPTURE_MPLANE);
            C(VIDEO_OUTPUT);
            C(VIDEO_OUTPUT_MPLANE);
            C(VIDEO_M2M);
            C(VIDEO_M2M_MPLANE);
            C(VIDEO_OVERLAY);
            C(VBI_CAPTURE);
            C(VBI_OUTPUT);
            C(SLICED_VBI_CAPTURE);
            C(SLICED_VBI_OUTPUT);
            C(RDS_CAPTURE);
            C(VIDEO_OUTPUT_OVERLAY);
            C(HW_FREQ_SEEK);
            C(RDS_OUTPUT);
            C(TUNER);
            C(AUDIO);
            C(RADIO);
            C(MODULATOR);
            C(SDR_CAPTURE);
            C(EXT_PIX_FORMAT);
            C(SDR_OUTPUT);
            C(META_CAPTURE);
            C(READWRITE);
            C(ASYNCIO);
            C(STREAMING);
            C(META_OUTPUT);
            C(TOUCH);
            C(IO_MC);
#undef C
            default: absl::PrintF(" ?0x%x?", bit);
        }
    }
}

// Scan all V4L2 video devices and print a line for each.
void scan_videodevs() {
    absl::PrintF("=== Scanning video devices ===\n");
    int found = 0;
    const std::filesystem::path v4l_dir = "/dev/v4l/by-path";
    for (const auto &entry : std::filesystem::directory_iterator(v4l_dir)) {
        const std::string filename = entry.path().filename();
        if (filename.find("-video-index") == std::string::npos) continue;

        const int fd = v4l2_open(entry.path().c_str(), O_RDWR);
        if (fd < 0) {
            absl::PrintF("*** Error opening: %s\n", entry.path());
            continue;
        }

        ++found;
        absl::PrintF("%s\n    ", entry.path());
        print_capability(fd);
        absl::PrintF("\n");
        v4l2_close(fd);
    }

    if (found) {
        absl::PrintF(
            "--- %d device(s); inspect with: pivid_list --video=<dev>\n\n",
            found
        );
    } else {
        absl::PrintF("No video devices found\n\n");
    }
}

// Print information about a V4L2 video device.
void inspect_videodev() {
    const auto path = absl::GetFlag(FLAGS_video);
    absl::PrintF("=== %s ===\n", path);

    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        absl::PrintF("*** Error opening: %s\n", path);
        exit(1);
    }
    if (v4l2_fd_open(fd, V4L2_DISABLE_CONVERSION) != fd) {
        absl::PrintF("*** Error in V4L2 open: %s\n", path);
        exit(1);
    }

    absl::PrintF("Driver: ");
    print_capability(fd);
    absl::PrintF("\n\n");

    for (int type = 0; type < V4L2_BUF_TYPE_PRIVATE; ++type) {
        v4l2_fmtdesc format = {};
        format.type = type;
        for (;;) {
            if (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &format)) break;

            if (format.index == 0) {
                switch (format.type) {
#define T(X) case V4L2_BUF_TYPE_##X: absl::PrintF("%s", #X); break
                    T(VIDEO_CAPTURE);
                    T(VIDEO_CAPTURE_MPLANE);
                    T(VIDEO_OUTPUT);
                    T(VIDEO_OUTPUT_MPLANE);
                    T(VIDEO_OVERLAY);
                    T(SDR_CAPTURE);
                    T(SDR_OUTPUT);
                    T(META_CAPTURE);
                    T(META_OUTPUT);
#undef T
                    default: absl::PrintF("?%d?", format.type); break;
                }
                absl::PrintF(" format(s):\n");
            }

            const std::string fourcc((const char *) &format.pixelformat, 4);
            const std::string desc((const char *) format.description);
            absl::PrintF("    %s", fourcc);
            if (desc != fourcc) absl::PrintF(" (%s)", desc);
            for (uint32_t bit = 1; bit != 0; bit <<= 1) {
                if (!(format.flags & bit)) continue;
                switch (bit) {
#define F(X) case V4L2_FMT_FLAG_##X: absl::PrintF(" %s", #X); break
                   F(COMPRESSED);
                   F(EMULATED);
                   F(CONTINUOUS_BYTESTREAM);
                   F(DYN_RESOLUTION);
                   F(ENC_CAP_FRAME_INTERVAL);
                   F(CSC_COLORSPACE);
                   F(CSC_XFER_FUNC);
                   F(CSC_YCBCR_ENC);
                   F(CSC_QUANTIZATION);
#undef F
                   default: absl::PrintF(" ?0x%x?", bit);
                }
            }
            absl::PrintF("\n");

            v4l2_frmsizeenum frame_size = {};
            frame_size.pixel_format = format.pixelformat;
            for (;;) {
                if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frame_size)) break;
                if (frame_size.index == 0) absl::PrintF("      ");
                if (frame_size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    const auto &size = frame_size.discrete;
                    absl::PrintF(" %dx%d", size.width, size.height);
                } else {
                    const auto &size = frame_size.stepwise;
                    absl::PrintF(
                        " %dx%d - %dx%d",
                        size.min_width, size.min_height,
                        size.max_width, size.max_height
                    );
                    if (size.step_width != 1 || size.step_height != 1) {
                        absl::PrintF(
                            " (by %dx%d)", size.step_width, size.step_height
                        );
                    }
                }

                ++frame_size.index;
            }
            if (frame_size.index > 0) absl::PrintF("\n");
            ++format.index;
        }
        if (format.index > 0) absl::PrintF("\n");
    }

    v4l2_input input = {};
    for (;;) {
        if (ioctl(fd, VIDIOC_ENUMINPUT, &input)) break;
        if (input.index == 0) absl::PrintF("Input(s):\n");
        absl::PrintF("    Inp #%d", input.index);
        switch (input.type) {
            case V4L2_INPUT_TYPE_TUNER: absl::PrintF(" TUNER"); break;
            case V4L2_INPUT_TYPE_CAMERA: absl::PrintF(" VIDEO"); break;
            case V4L2_INPUT_TYPE_TOUCH: absl::PrintF(" TOUCH"); break;
            default: absl::PrintF(" ?%d?", input.type); break;
        }
        absl::PrintF(" \"%s\"\n", (const char *) input.name);
        ++input.index;
    }
    if (input.index > 0) absl::PrintF("\n");

    v4l2_output output = {};
    for (;;) {
        if (ioctl(fd, VIDIOC_ENUMOUTPUT, &output)) break;
        if (output.index == 0) absl::PrintF("Output(s):\n");
        absl::PrintF("    Out #%d", output.index);
        switch (output.type) {
            case V4L2_OUTPUT_TYPE_MODULATOR: absl::PrintF(" MODULATOR"); break;
            case V4L2_OUTPUT_TYPE_ANALOG: absl::PrintF(" VIDEO"); break;
            case V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY: absl::PrintF(" OVL"); break;
            default: absl::PrintF(" ?%d?", output.type); break;
        }
        absl::PrintF(" \"%s\"\n", (const char *) output.name);
        ++output.index;
    }
    if (output.index > 0) absl::PrintF("\n");

    v4l2_close(fd);
}

int main(const int argc, char** const argv) {
    absl::ParseCommandLine(argc, argv);
    if (!absl::GetFlag(FLAGS_gpu).empty()) {
        inspect_gpu();  // Show resources for one card
    } else if (!absl::GetFlag(FLAGS_video).empty()) {
        inspect_videodev();
    } else {
        scan_gpus();       // Without --gpu, summarize all GPUs
        scan_videodevs();  // Without --video, summarize all video devices
    }

    return 0;
}
