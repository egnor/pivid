// Simple command line tool to list V4L devices.

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
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

// Print driver name and capability bits from VIDIOC_QUERYCAP results.
void print_videodev_driver(int const fd) {
    v4l2_capability cap = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        absl::PrintF("*** Error querying device\n");
        exit(1);
    }

    uint32_t const v = cap.version;
    absl::PrintF(
        "%s v%d.%d.%d:",
        (char const*) cap.driver, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    );

    auto const caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
    for (uint32_t bit = 1; bit > 0; bit <<= 1) {
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
            default: absl::PrintF(" ?0x%x?", bit); break;
        }
    }
}

// Scan all V4L2 video devices and print a line for each.
void scan_videodevs() {
    absl::PrintF("=== Scanning V4L video I/O devices ===\n");
    std::filesystem::path const dev_dir = "/dev";
    std::vector<std::string> dev_files;
    for (auto const& entry : std::filesystem::directory_iterator(dev_dir)) {
        std::string const filename = entry.path().filename();
        if (filename.substr(0, 5) == "video" && isdigit(filename[5]))
            dev_files.push_back(entry.path().native());
    }

    std::sort(dev_files.begin(), dev_files.end());
    for (auto const &path : dev_files) {
        int const fd = open(path.c_str(), O_RDWR);
        if (fd < 0) {
            absl::PrintF("*** %s: %s\n", path, strerror(errno));
            continue;
        }

        absl::PrintF("%s\n    ", path);
        print_videodev_driver(fd);
        absl::PrintF("\n");
        close(fd);
    }

    if (dev_files.empty()) {
        absl::PrintF("*** No V4L devices found\n");
    } else {
        absl::PrintF(
            "--- %d V4L device(s); inspect with --dev=<dev>\n",
            dev_files.size()
        );
    }
}

// Print information about a V4L2 video device.
void inspect_videodev(std::string const& path) {
    absl::PrintF("=== %s ===\n", path);

    int const fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        absl::PrintF("*** %s: %s\n", path, strerror(errno));
        exit(1);
    }

    absl::PrintF("Driver: ");
    print_videodev_driver(fd);
    absl::PrintF("\n\n");

    for (int type = 0; type < V4L2_BUF_TYPE_PRIVATE; ++type) {
        v4l2_fmtdesc format = {};
        for (
            format.type = type;
            ioctl(fd, VIDIOC_ENUM_FMT, &format) >= 0;
            ++format.index
        ) {
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
                absl::PrintF(":");

                v4l2_requestbuffers buffers = {};
                buffers.count = 0;  // Query capbilities only
                buffers.type = format.type;
                buffers.memory = V4L2_MEMORY_MMAP;
                if (!ioctl(fd, VIDIOC_REQBUFS, &buffers)) {
                    for (uint32_t bit = 1; bit > 0; bit <<= 1) {
                        if (!(buffers.capabilities & bit)) continue;
                        switch (bit) {
#define C(X) case V4L2_BUF_CAP_SUPPORTS_##X: absl::PrintF(" %s", #X); break
                            C(MMAP);
                            C(USERPTR);
                            C(DMABUF);
                            C(REQUESTS);
                            C(ORPHANED_BUFS);
                            C(M2M_HOLD_CAPTURE_BUF);
                            C(MMAP_CACHE_HINTS);
#undef C
                            default: absl::PrintF(" ?0x%x?", bit); break;
                        }
                    }
                }
                absl::PrintF("\n");
            }

            std::string const fourcc((char const*) &format.pixelformat, 4);
            std::string const desc((char const*) format.description);
            absl::PrintF("    %s", fourcc);
            for (uint32_t bit = 1; bit > 0; bit <<= 1) {
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
                   default: absl::PrintF(" ?0x%x?", bit); break;
                }
            }
            if (desc != fourcc) absl::PrintF(" (%s)", desc);

            v4l2_frmsizeenum size = {};
            size.pixel_format = format.pixelformat;
            while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) >= 0) {
                if (size.index % 6 == 0) absl::PrintF("\n       ");
                if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    auto const& dim = size.discrete;
                    absl::PrintF(" %dx%d", dim.width, dim.height);
                } else {
                    auto const& dim = size.stepwise;
                    absl::PrintF(
                        " %dx%d - %dx%d",
                        dim.min_width, dim.min_height,
                        dim.max_width, dim.max_height
                    );
                    if (dim.step_width != 1 || dim.step_height != 1) {
                        absl::PrintF(
                            " ±%dx%d", dim.step_width, dim.step_height
                        );
                    }
                }
                ++size.index;
            }

            absl::PrintF("\n");
        }
        if (format.index > 0) absl::PrintF("\n");
    }

    v4l2_input input = {};
    for (; !ioctl(fd, VIDIOC_ENUMINPUT, &input); ++input.index) {
        if (input.index == 0) absl::PrintF("Inputs:\n");
        absl::PrintF("    Inp #%d", input.index);
        switch (input.type) {
#define I(X, y) case V4L2_INPUT_TYPE_##X: absl::PrintF(" %s%s", #X, y); break
            I(TUNER, "");
            I(CAMERA, "/video");
            I(TOUCH, "");
#undef I
            default: absl::PrintF(" ?{}?", input.type); break;
        }
        absl::PrintF(" (%s)\n", (char const*) input.name);
    }
    if (input.index > 0) absl::PrintF("\n");

    v4l2_output output = {};
    for (; !ioctl(fd, VIDIOC_ENUMOUTPUT, &output); ++output.index) {
        if (output.index == 0) absl::PrintF("Outputs:\n");
        absl::PrintF("    Out #%d", output.index);
        switch (output.type) {
#define O(X, y) case V4L2_OUTPUT_TYPE_##X: absl::PrintF(" %s%s", #X, y); break
            O(MODULATOR, "");
            O(ANALOG, "/video");
            O(ANALOGVGAOVERLAY, "/overlay");
#undef O
            default: absl::PrintF(" ?%d?", output.type); break;
        }
        absl::PrintF(" (%s)\n", (char const*) output.name);
    }
    if (output.index > 0) absl::PrintF("\n");

    v4l2_query_ext_ctrl ctrl = {};
    ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    int found = 0;
    for (
        ;
        ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &ctrl) >= 0;
        ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND
    ) {
        if (!found++) absl::PrintF("Controls:\n");
        absl::PrintF("    Ctrl 0x%x", ctrl.id);
        switch (ctrl.type) {
#define T(X) case V4L2_CTRL_TYPE_##X: absl::PrintF(" %<7s", #X); break
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
            default: absl::PrintF(" ?%d?", ctrl.type); break;
        }

        if (ctrl.minimum || ctrl.maximum) {
            absl::PrintF(" %5d-%-4d", ctrl.minimum, ctrl.maximum);
            if (ctrl.step > 1) absl::PrintF(" ±%d", ctrl.step);
        }
        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if (!(ctrl.flags & bit)) continue;
            switch (bit) {
#define F(X) case V4L2_CTRL_FLAG_##X: absl::PrintF(" %s", #X); break
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
        absl::PrintF(" (%s)\n", ctrl.name);

        if (ctrl.type == V4L2_CTRL_TYPE_MENU) {
            v4l2_querymenu item = {};
            item.id = ctrl.id;
            for (; ioctl(fd, VIDIOC_QUERYMENU, &item) >= 0; ++item.index) {
                absl::PrintF(
                    "        %d: %s\n",
                    int(item.index), (char const*) item.name
                );
            }
        }
    }
    if (found > 0) absl::PrintF("\n");
    close(fd);
}

ABSL_FLAG(std::string, dev, "", "Video device (in /dev/v4l) to inspect");

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage("Inspect kernel video (V4L2) devices");
    absl::ParseCommandLine(argc, argv);
    if (!absl::GetFlag(FLAGS_dev).empty()) {
        inspect_videodev(absl::GetFlag(FLAGS_dev));
    } else {
        scan_videodevs();
    }
    return 0;
}
