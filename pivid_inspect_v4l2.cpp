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

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

// Print driver name and capability bits from VIDIOC_QUERYCAP results.
std::string describe_driver(v4l2_capability const& cap) {
    uint32_t const v = cap.version;
    std::string out = fmt::format(
        "{} v{}.{}.{}:",
        (char const*) cap.driver, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF
    );

    auto const caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? cap.device_caps : cap.capabilities;
    for (uint32_t bit = 1; bit > 0; bit <<= 1) {
        if (!(caps & bit)) continue;
        switch (bit) {
#define C(X) case V4L2_CAP_##X: out += fmt::format(" {}", #X); break
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
            default: out += fmt::format(" ?0x{:x}?", bit); break;
        }
    }
    return out;
}

// Scan all V4L2 video devices and print a line for each.
void scan_videodevs() {
    fmt::print("=== Scanning V4L video I/O devices ===\n");
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
            fmt::print("*** {}: {}\n", path, strerror(errno));
            continue;
        }

        v4l2_capability cap = {};
        if (!ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
            fmt::print(
                "{}: {} ({})\n",
                path, (char*) cap.bus_info, (char*) cap.card
            );
            fmt::print("    {}\n", describe_driver(cap));
        }

        close(fd);
    }

    if (dev_files.empty()) {
        fmt::print("*** No V4L devices found\n");
    } else {
        fmt::print(
            "--- {} V4L device(s); inspect with --dev=<dev>\n",
            dev_files.size()
        );
    }
}

// Print information about a V4L2 video device.
void inspect_videodev(std::string const& path) {
    fmt::print("=== {} ===\n", path);

    int const fd = open(path.c_str(), O_RDWR);
    if (fd < 0) {
        fmt::print("*** {}: {}\n", path, strerror(errno));
        exit(1);
    }

    v4l2_capability cap = {};
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        fmt::print("*** Querying: {}\n", strerror(errno));
        exit(1);
    }

    fmt::print("Driver: {}\n", describe_driver(cap));
    fmt::print("Device: {} ({})\n", (char*) cap.bus_info, (char*) cap.card);
    fmt::print("\n");

    for (int type = 0; type < V4L2_BUF_TYPE_PRIVATE; ++type) {
        v4l2_fmtdesc format = {};
        for (
            format.type = type;
            ioctl(fd, VIDIOC_ENUM_FMT, &format) >= 0;
            ++format.index
        ) {
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
                fmt::print(":");

                v4l2_requestbuffers buffers = {};
                buffers.count = 0;  // Query capbilities only
                buffers.type = format.type;
                buffers.memory = V4L2_MEMORY_MMAP;
                if (!ioctl(fd, VIDIOC_REQBUFS, &buffers)) {
                    for (uint32_t bit = 1; bit > 0; bit <<= 1) {
                        if (!(buffers.capabilities & bit)) continue;
                        switch (bit) {
#define C(X) case V4L2_BUF_CAP_SUPPORTS_##X: fmt::print(" {}", #X); break
                            C(MMAP);
                            C(USERPTR);
                            C(DMABUF);
                            C(REQUESTS);
                            C(ORPHANED_BUFS);
                            C(M2M_HOLD_CAPTURE_BUF);
                            C(MMAP_CACHE_HINTS);
#undef C
                            default: fmt::print(" ?0x{:x}?", bit); break;
                        }
                    }
                }
                fmt::print("\n");
            }

            std::string const fourcc((char const*) &format.pixelformat, 4);
            std::string const desc((char const*) format.description);
            fmt::print("    {}", fourcc);
            for (uint32_t bit = 1; bit > 0; bit <<= 1) {
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
                   default: fmt::print(" ?0x{:x}?", bit); break;
                }
            }
            if (desc != fourcc) fmt::print(" ({})", desc);

            v4l2_frmsizeenum size = {};
            size.pixel_format = format.pixelformat;
            while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) >= 0) {
                if (size.index % 6 == 0) fmt::print("\n       ");
                if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                    auto const& dim = size.discrete;
                    fmt::print(" {}x{}", dim.width, dim.height);
                } else {
                    auto const& dim = size.stepwise;
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

            fmt::print("\n");
        }
        if (format.index > 0) fmt::print("\n");
    }

    v4l2_input input = {};
    for (; !ioctl(fd, VIDIOC_ENUMINPUT, &input); ++input.index) {
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
        fmt::print(" ({})\n", (char const*) input.name);
    }
    if (input.index > 0) fmt::print("\n");

    v4l2_output output = {};
    for (; !ioctl(fd, VIDIOC_ENUMOUTPUT, &output); ++output.index) {
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
        fmt::print(" ({})\n", (char const*) output.name);
    }
    if (output.index > 0) fmt::print("\n");

    v4l2_query_ext_ctrl ctrl = {};
    ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
    int found = 0;
    for (
        ;
        ioctl(fd, VIDIOC_QUERY_EXT_CTRL, &ctrl) >= 0;
        ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND
    ) {
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
            fmt::print(" {:>4}-{:<4}", ctrl.minimum, ctrl.maximum);
            if (ctrl.step > 1) fmt::print(" ±{}", ctrl.step);
        }
        for (uint32_t bit = 1; bit > 0; bit <<= 1) {
            if (!(ctrl.flags & bit)) continue;
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
        fmt::print(" ({})\n", ctrl.name);

        if (ctrl.type == V4L2_CTRL_TYPE_MENU) {
            v4l2_querymenu item = {};
            item.id = ctrl.id;
            for (; ioctl(fd, VIDIOC_QUERYMENU, &item) >= 0; ++item.index) {
                fmt::print(
                    "        {}: {}\n",
                    int(item.index), (char const*) item.name
                );
            }
        }
    }
    if (found > 0) fmt::print("\n");
    close(fd);
}

int main(int argc, char** argv) {
    std::string dev;

    CLI::App app("Inspect kernel video (V4L2) devices");
    app.add_option("--dev", dev, "Video device (in /dev/v4l) to inspect");
    CLI11_PARSE(app, argc, argv);

    if (!dev.empty()) {
        inspect_videodev(dev);
    } else {
        scan_videodevs();
    }
    return 0;
}
