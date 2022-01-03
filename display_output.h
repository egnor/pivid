#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "frame_buffer.h"
#include "unix_system.h"

namespace pivid {

struct DisplayDriverListing {
    std::string dev_file; 
    std::string system_path;
    std::string driver;
    std::string driver_date;
    std::string driver_desc;
    std::string driver_bus_id;
};

struct DisplayMode {
    struct Timings {
        int display, sync_start, sync_end, total;
        int doubling, sync_polarity;
        auto operator<=>(Timings const&) const = default;
    };

    std::string name;
    int pixel_khz;
    int refresh_hz;
    Timings horiz, vert;
    auto operator<=>(DisplayMode const&) const = default;
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
    double fb_x, fb_y, fb_width, fb_height;
    int out_x, out_y, out_width, out_height;
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayStatus> scan_outputs() = 0;
    virtual bool ready_for_update(uint32_t connector_id) = 0;
    virtual void update_output(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) = 0;
};

std::vector<DisplayDriverListing> list_display_drivers(UnixSystem* sys);

std::unique_ptr<DisplayDriver> open_display_driver(
    UnixSystem* sys, std::string const& dev_file
);

std::string debug_string(DisplayDriverListing const&);
std::string debug_string(DisplayMode const&);

}  // namespace pivid
