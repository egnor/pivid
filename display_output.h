#pragma once

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "frame_buffer.h"

namespace pivid {

struct DisplayMode {
    struct Timings {
        int clock;
        int display, sync_start, sync_end, total;
        int sync_polarity;
    };

    Timings horiz, vert;
    int pixel_skew;
    int line_repeats;
    bool interlace;
    int clock_exp2;
    int csync_polarity;
    std::string name;
    bool preferred;

    std::string format() const;
};

struct DisplayOutputStatus {
    uint32_t connector_id;
    std::string name;
    std::optional<bool> connected;
    std::vector<DisplayMode> modes;
    std::optional<DisplayMode> active_mode;  // Output disabled if not present.
};

struct DisplayOutputUpdate {
    struct Layer {
        FrameBuffer fb;
        double fb_x, fb_y, fb_w, fb_h;
        int out_x, out_y, out_w, out_h;
    };

    uint32_t connector_id;
    std::optional<DisplayMode> mode;
    std::vector<Layer> layers;
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayOutputStatus> scan_outputs() = 0;
    virtual void make_updates(std::vector<DisplayOutputUpdate> const&) = 0;
};

std::unique_ptr<DisplayDriver> open_display_driver(
    std::filesystem::path const& dev_file
);

struct DisplayDriverListing {
    std::filesystem::path dev_file; 
    std::string system_path;
    std::string driver;
    std::string driver_date;
    std::string driver_desc;
    std::string driver_bus_id;
};

std::vector<DisplayDriverListing> list_display_drivers();

}  // namespace pivid
