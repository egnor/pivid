#pragma once

#include <memory>
#include <string>
#include <ostream>
#include <vector>

#include "image_buffer.h"
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
        int display = 0, sync_start = 0, sync_end = 0, total = 0;
        int doubling = 0, sync_polarity = 0;
    };

    std::string name;
    int pixel_khz = 0;
    int refresh_hz = 0;
    Timings horiz, vert;
};

struct DisplayConnectorStatus {
    uint32_t id = 0;
    std::string name;
    bool display_detected = false;
    std::vector<DisplayMode> display_modes;
    DisplayMode active_mode;
};

struct DisplayLayer {
    double source_x = 0, source_y = 0, source_width = 0, source_height = 0;
    int screen_x = 0, screen_y = 0, screen_width = 0, screen_height = 0;
    std::shared_ptr<uint32_t const> source;
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayConnectorStatus> scan_connectors() = 0;
    virtual std::shared_ptr<uint32_t const> load_image(ImageBuffer) = 0;

    virtual bool update_if_ready(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers
    ) = 0;
};

std::vector<DisplayDriverListing> list_display_drivers(UnixSystem* sys);
std::unique_ptr<DisplayDriver> open_display_driver(
    UnixSystem* sys, std::string const& dev_file
);

std::string debug(DisplayDriverListing const&);
std::string debug(DisplayMode const&);

}  // namespace pivid
