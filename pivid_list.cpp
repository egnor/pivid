// Simple command line tool to list DRM/KMS resources and their IDs.

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>

#include <filesystem>
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>
#include <libv4l2.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

DEFINE_string(gpu, "", "GPU device (in /dev/dri) to inspect");
DEFINE_string(video, "", "Video device (in /dev/v4l) to inspect");
DEFINE_bool(detail, false, "Print detailed object properties");

//
// GPUs
//

// Scan all DRM/KMS capable video cards and print a line for each.
void scan_gpus() {
    fmt::print("=== Scanning GPUs ===\n");
    int found = 0;
    const std::filesystem::path dri_dir = "/dev/dri";
    for (const auto &entry : std::filesystem::directory_iterator(dri_dir)) {
        const std::string filename = entry.path().filename();
        if (filename.substr(0, 4) != "card") continue;

        const int fd = open(entry.path().c_str(), O_RDWR);
        if (fd < 0) {
            fmt::print("*** Error opening: {}\n", entry.path().native());
            continue;
        }

        auto* const ver = drmGetVersion(fd);
        if (!ver) {
            fmt::print("*** {}: Error reading version\n", filename);
        } else {
            ++found;
            fmt::print("{}", entry.path().native());

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

    if (found) {
        fmt::print(
            "--- {} GPU(s); inspect with: pivid_list --gpu=<dev>\n\n",
            found
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
void inspect_gpu() {
    const auto& path = FLAGS_gpu;
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
        if (FLAGS_detail) print_gpu_object_properties(fd, id);
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
        if (FLAGS_detail) print_gpu_object_properties(fd, id);
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
        if (FLAGS_detail) print_gpu_object_properties(fd, id);
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
        if (FLAGS_detail) print_gpu_object_properties(fd, id);
        drmModeFreeConnector(conn);
    }
    fmt::print("\n");
    drmFreeVersion(ver);
    drmModeFreeResources(res);
    close(fd);
}

//
// Video devices
//

// Print driver name and capability bits from VIDIOC_QUERYCAP results.
void print_videodev_driver(const int fd) {
    v4l2_capability cap = {};
    if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        fmt::print("*** Error querying device\n");
        exit(1);
    }

    const uint32_t v = cap.version;
    fmt::print(
        "{} v{}.{}.{}:",
        (const char *) cap.driver, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    );

    const auto caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
    for (uint32_t bit = 1; bit != 0; bit <<= 1) {
        if (!(caps & bit)) continue;
        switch (bit) {
#define C(X) case V4L2_CAP_##X: fmt::print(" {}", #X); break
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
            default: fmt::print(" ?0x{:x}?", bit);
        }
    }
}

// Scan all V4L2 video devices and print a line for each.
void scan_videodevs() {
    fmt::print("=== Scanning video devices ===\n");
    int found = 0;
    const std::filesystem::path v4l_dir = "/dev/v4l/by-path";
    for (const auto &entry : std::filesystem::directory_iterator(v4l_dir)) {
        const std::string filename = entry.path().filename();
        if (filename.find("-video-index") == std::string::npos) continue;

        const int fd = v4l2_open(entry.path().c_str(), O_RDWR);
        if (fd < 0) {
            fmt::print("*** Error opening: {}\n", entry.path().native());
            continue;
        }

        ++found;
        fmt::print("{}\n    ", entry.path().native());
        print_videodev_driver(fd);
        fmt::print("\n");
        v4l2_close(fd);
    }

    if (found) {
        fmt::print(
            "--- {} device(s); inspect with: pivid_list --video=<dev>\n\n",
            found
        );
    } else {
        fmt::print("No video devices found\n\n");
    }
}

// Print information about a V4L2 video device.
void inspect_videodev() {
    const auto& path = FLAGS_video;
    fmt::print("=== {} ===\n", path);

    const int fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        fmt::print("*** Error opening: {}\n", path);
        exit(1);
    }
    if (v4l2_fd_open(fd, V4L2_DISABLE_CONVERSION) != fd) {
        fmt::print("*** Error in V4L2 open: {}\n", path);
        exit(1);
    }

    fmt::print("Driver: ");
    print_videodev_driver(fd);
    fmt::print("\n\n");

    for (int type = 0; type < V4L2_BUF_TYPE_PRIVATE; ++type) {
        v4l2_fmtdesc format = {};
        format.type = type;
        for (;;) {
            if (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &format)) break;

            if (format.index == 0) {
                switch (format.type) {
#define T(X) case V4L2_BUF_TYPE_##X: fmt::print("{}", #X); break
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
                    default: fmt::print("?{}?", format.type); break;
                }
                fmt::print(" formats:\n");
            }

            const std::string fourcc((const char *) &format.pixelformat, 4);
            const std::string desc((const char *) format.description);
            fmt::print("    {}", fourcc);
            for (uint32_t bit = 1; bit != 0; bit <<= 1) {
                if (!(format.flags & bit)) continue;
                switch (bit) {
#define F(X) case V4L2_FMT_FLAG_##X: fmt::print(" {}", #X); break
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
                   default: fmt::print(" ?0x{:x}?", bit);
                }
            }
            if (desc != fourcc) fmt::print(" ({})", desc);

            if (FLAGS_detail) {
                v4l2_frmsizeenum size = {};
                size.pixel_format = format.pixelformat;
                for (;;) {
                    if (v4l2_ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size)) break;
                    if (size.index % 6 == 0) fmt::print("\n       ");
                    if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                        const auto &dim = size.discrete;
                        fmt::print(" {}x{}", dim.width, dim.height);
                    } else {
                        const auto &dim = size.stepwise;
                        fmt::print(
                            " {}x{} - {}x{}",
                            dim.min_width, dim.min_height,
                            dim.max_width, dim.max_height
                        );
                        if (dim.step_width != 1 || dim.step_height != 1) {
                            fmt::print(
                                " ±{}x{}", dim.step_width, dim.step_height
                            );
                        }
                    }

                    ++size.index;
                }
            }
            fmt::print("\n");
            ++format.index;
        }
        if (format.index > 0) fmt::print("\n");
    }

    v4l2_input input = {};
    for (;;) {
        if (v4l2_ioctl(fd, VIDIOC_ENUMINPUT, &input)) break;
        if (input.index == 0) fmt::print("Inputs:\n");
        fmt::print("    Inp #{}", input.index);
        switch (input.type) {
#define I(X, y) case V4L2_INPUT_TYPE_##X: fmt::print(" {}{}", #X, y); break
            I(TUNER, "");
            I(CAMERA, "/video");
            I(TOUCH, "");
#undef I
            default: fmt::print(" ?{}?", input.type); break;
        }
        fmt::print(" ({})\n", (const char *) input.name);
        ++input.index;
    }
    if (input.index > 0) fmt::print("\n");

    v4l2_output output = {};
    for (;;) {
        if (v4l2_ioctl(fd, VIDIOC_ENUMOUTPUT, &output)) break;
        if (output.index == 0) fmt::print("Outputs:\n");
        fmt::print("    Out #{}", output.index);
        switch (output.type) {
#define O(X, y) case V4L2_OUTPUT_TYPE_##X: fmt::print(" {}{}", #X, y); break
            O(MODULATOR, "");
            O(ANALOG, "/video");
            O(ANALOGVGAOVERLAY, "/overlay");
#undef O
            default: fmt::print(" ?{}?", output.type); break;
        }
        fmt::print(" ({})\n", (const char *) output.name);
        ++output.index;
    }
    if (output.index > 0) fmt::print("\n");

    if (FLAGS_detail) {
        v4l2_query_ext_ctrl ctrl = {};
        int found = 0;
        for (;;) {
            ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
            if (v4l2_ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &ctrl)) break;
            if (!found++) fmt::print("Controls:\n");
            fmt::print("    Ctrl 0x{:x}", ctrl.id);
            switch (ctrl.type) {
#define T(X) case V4L2_CTRL_TYPE_##X: fmt::print(" {:<7}", #X); break
                T(INTEGER);
                T(BOOLEAN);
                T(MENU);
                T(BUTTON);
                T(INTEGER64);
                T(CTRL_CLASS);
                T(STRING);
                T(BITMASK);
                T(INTEGER_MENU);
                T(U8);
                T(U16);
                T(U32);
                T(AREA);
#undef T
                default: fmt::print(" ?{}?", ctrl.type); break;
            }

            if (ctrl.minimum || ctrl.maximum) {
                fmt::print(" {:<4}-{:<4}", ctrl.minimum, ctrl.maximum);
                if (ctrl.step > 1) fmt::print(" ±{}", ctrl.step);
            }
            for (uint32_t bit = 1; bit > 0; bit <<= 1) {
                if (ctrl.flags & bit) {
                    switch (bit) {
#define F(X) case V4L2_CTRL_FLAG_##X: fmt::print(" {}", #X); break
                        F(DISABLED);
                        F(GRABBED);
                        F(READ_ONLY);
                        F(UPDATE);
                        F(INACTIVE);
                        F(SLIDER);
                        F(WRITE_ONLY);
                        F(VOLATILE);
                        F(HAS_PAYLOAD);
                        F(EXECUTE_ON_WRITE);
                        F(MODIFY_LAYOUT);
#undef F
                    }
                }
            }
            fmt::print(" ({})\n", ctrl.name);

            if (ctrl.type == V4L2_CTRL_TYPE_MENU) {
                v4l2_querymenu item = {};
                item.id = ctrl.id;
                for (;;) {
                    if (v4l2_ioctl(fd, VIDIOC_QUERYMENU, &item)) break;
                    fmt::print(
                        "        {}: {}\n",
                        int(item.index), (const char *) item.name
                    );
                    ++item.index;
                }
            }
        }
        if (found > 0) fmt::print("\n");
    }

    v4l2_close(fd);
}

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (!FLAGS_gpu.empty()) {
        inspect_gpu();  // Show resources for one card
    } else if (!FLAGS_video.empty()) {
        inspect_videodev();
    } else {
        scan_gpus();       // Without --gpu, summarize all GPUs
        scan_videodevs();  // Without --video, summarize all video devices
    }

    return 0;
}
