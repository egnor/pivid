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
        int display, sync_start, sync_end, total;
        int doubling, sync_polarity;
    };

    std::string name;
    int pixel_khz;
    int refresh_hz;
    Timings horiz, vert;
};

struct DisplayStatus {
    uint32_t connector_id;
    std::string connector_name;
    bool display_detected;
    std::vector<DisplayMode> display_modes;
    DisplayMode active_mode;
};

struct DisplayLayer {
    FrameBuffer fb;
    double fb_x, fb_y, fb_w, fb_h;
    int out_x, out_y, out_w, out_h;
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayStatus> scan_outputs() = 0;
    virtual bool update_ready(uint32_t connector_id) = 0;
    virtual void update_output(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) = 0;
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

std::string debug_string(DisplayDriverListing const&);
std::string debug_string(DisplayMode const&);

}  // namespace pivid
