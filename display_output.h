#pragma once

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pivid {

class DisplayError : public std::exception {};

struct DisplayMode {
    struct Timings {
        uint32_t clock;
        uint16_t display, sync_start, sync_end, total;
        int8_t sync_polarity;
    };

    Timings horiz, vert;
    uint16_t pixel_skew;
    uint16_t line_repeats;
    bool interlace;
    int8_t clock_exp2;
    int8_t csync_polarity;
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

struct DisplayOutputRequest {
    uint32_t connector_id;
    std::optional<DisplayMode> mode;  // Output disabled if not present.
};

class DisplayDriver {
  public:
    virtual ~DisplayDriver() {}
    virtual std::vector<DisplayOutputStatus> scan_outputs() = 0;
    virtual void set_outputs(std::vector<DisplayOutputRequest> const&) = 0;
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
