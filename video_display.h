#pragma once

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pivid {

class DisplayError : public std::exception {};

class DisplayConnector {
  public:
    virtual ~DisplayConnector() {}
};

struct DisplayMode {
    struct Timings {
        uint32_t clock;
        uint16_t display;
        uint16_t sync_start;
        uint16_t sync_end;
        uint16_t total;
        int8_t sync;
    };

    Timings horiz, vert;
    uint16_t pixel_skew;
    uint16_t line_reps;
    bool interlace;
    int8_t clock_exp2;
    int8_t composite_sync;

    std::string name;
    bool preferred;
};

struct DisplayConnectorListing {
    uint32_t id;
    std::string type;
    int which;
    std::optional<bool> connected;
    std::vector<DisplayMode> modes;
    std::optional<DisplayMode> active_mode;
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayConnectorListing> list_connectors() = 0;
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

std::vector<DisplayDriverListing> list_drivers();

}  // namespace pivid
