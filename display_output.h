// Interfaces used to display and overlay images on-screen.

#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <optional>
#include <ostream>
#include <vector>

#include "image_buffer.h"
#include "unix_system.h"

namespace pivid {

// Description of video mode resolution & timings (like XFree86 Modeline).
// Available modes come from DisplayDriver::scan_connectors();
// the desired mode to use is given to DisplayDriver::request_update().
// (A custom/tweaked mode may also be used if you're wild and crazy.)
struct DisplayMode {
    struct Timings {
        // Values are in pixels for horiz, scanlines for vert.
        int display = 0, sync_start = 0, sync_end = 0, total = 0;
        int doubling = 0;       // 2 for pixel/scanline doubling
        int sync_polarity = 0;  // +1 or -1 for sync pulse priority
    };

    std::string name;     // Like "1920x1080" (doesn't capture detail)
    Timings horiz, vert;  // Screen size is horiz.display x vert.display
    int pixel_khz = 0;    // Basic pixel clock
    int refresh_hz = 0;   // Refresh rate (like 30 or 60)
};

// Current connector state and recommended modes based on monitor data (EDID).
// Returned by scan_connectors().
struct DisplayStatus {
    uint32_t id = 0;
    std::string name;               // Like "HDMI-1"
    bool display_detected = false;  // True if a monitor is connected
    DisplayMode active_mode;
    std::vector<DisplayMode> display_modes;  // First mode is the "best".
};

// Update parameters to define a connector's video mode & screen contents.
// Passed to show_frame(), where it takes effect at the next vsync.
// All images on screen must be given every time (they are not "sticky"). 
struct DisplayFrame {
    // One image (or portion thereof) and its placement on screen.
    struct Content {
        std::shared_ptr<uint32_t const> loaded_image;  // From load_image()
        double from_x = 0, from_y = 0, from_width = 0, from_height = 0;
        int to_x = 0, to_y = 0, to_width = 0, to_height = 0;
    };

    uint32_t connector_id;          // From DisplayStatus::id
    DisplayMode mode;               // From DisplayStatus::display_modes
    std::vector<Content> contents;  // Image layers in bottom to top Z-order
};

// Returned by DisplayDriver::is_frame_shown() after a frame has become visible.
struct DisplayFrameStatus {
    std::chrono::steady_clock::time_point update_time;  // Time of vsync flip
    std::optional<ImageBuffer> writeback;  // Output for WRITEBACK-* connectors
};

// Interface to a GPU device. Normally one per system, handling all outputs.
// Returned by open_display_driver().
class DisplayDriver {
  public:
    virtual ~DisplayDriver() = default;

    // Returns the ID, name, and current status of all connectors.
    virtual std::vector<DisplayStatus> scan_connectors() = 0;

    // Imports an image into the GPU for use in DisplayUpdateRequest.
    virtual std::shared_ptr<uint32_t const> load_image(ImageBuffer) = 0;

    // Updates a connector's video mode & screen contents at the next vsync.
    // Do not call again until the update completes (per is_frame_shown()).
    virtual void show_frame(DisplayFrame const&) = 0;

    // Returns {} if an update is pending, otherwise returns update status.
    virtual std::optional<DisplayFrameStatus> is_frame_shown(uint32_t id) = 0;
};

// Description of a GPU device. Returned by list_device_drivers().
struct DisplayDriverListing {
    std::string dev_file;       // Like "/dev/dri/card0"
    std::string system_path;    // Like "platform/gpu/drm/card0" (more stable)
    std::string driver;         // Like "vc4" or "i915"
    std::string driver_date;    // Like "20140616" (first development date)
    std::string driver_desc;    // Like "Broadcom VC4 graphics"
    std::string driver_bus_id;  // Like "fec00000.v3d" (PCI address, etc)
};

// Lists GPU devices present on the system (typically only one).
std::vector<DisplayDriverListing> list_display_drivers(
    std::shared_ptr<UnixSystem> const& sys
);

// Opens a GPU device for use, given dev_file from DisplayDriverListing.
// Only one process on the system can do this at a time.
// (The screen must be on a text console, not a desktop environment.)
std::unique_ptr<DisplayDriver> open_display_driver(
    std::shared_ptr<UnixSystem> sys, std::string const& dev_file
);

// Debugging descriptions of structures. TODO: Add more?
std::string debug(DisplayDriverListing const&);
std::string debug(DisplayMode const&);

}  // namespace pivid
