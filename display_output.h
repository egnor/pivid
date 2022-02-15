// Interfaces to display and overlay images on-screen.

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
    std::string name;       // Like "1920x1080" (doesn't capture detail)
    XY<int> size;           // Displayable pixel size
    XY<int> scan_size;      // Overall timing size
    XY<int> sync_start;     // Horiz / vert sync start
    XY<int> sync_end;       // Horiz / vert sync pulse end
    XY<int> sync_polarity;  // Horiz / vert sync polarity (+1 / -1)
    XY<int> doubling;       // Clock doubling / doublescan / interlace (+1 / -1)
    int pixel_khz = 0;      // Basic pixel clock
    int refresh_hz = 0;     // Refresh rate (like 30 or 60)
};

// Current connector state and recommended modes based on monitor data (EDID).
// Returned by scan_connectors().
struct DisplayConnector {
    uint32_t id = 0;
    std::string name;               // Like "HDMI-1"
    bool display_detected = false;  // True if a monitor is connected
    DisplayMode active_mode;
    std::vector<DisplayMode> modes;  // First mode is the "best".
};

// Where one image (or a portion thereof) should be shown on screen
struct DisplayLayer {
    std::shared_ptr<LoadedImage> image;  // From DisplayDriver::load_image()
    XY<double> from = {}, from_size = {};
    XY<int> to = {}, to_size = {};
    // TODO: Transparency, rotation?
};

// Returned by DisplayDriver::is_frame_shown() after a frame has become visible.
struct DisplayUpdateDone {
    std::chrono::steady_clock::time_point time;  // Time of vsync flip
    std::optional<ImageBuffer> writeback;  // Output for writeback connectors
};

// Interface to a GPU device. Normally one per system, handling all outputs.
// Returned by open_display_driver().
// *Internally synchronized* for multithreaded access.
class DisplayDriver {
  public:
    virtual ~DisplayDriver() = default;

    // Returns the ID, name, and current status of all connectors.
    virtual std::vector<DisplayConnector> scan_connectors() = 0;

    // Imports an image into the GPU for use in DisplayUpdateRequest.
    virtual std::shared_ptr<LoadedImage> load_image(ImageBuffer) = 0;

    // Updates a connector's screen contents &/or video mode at the next vsync.
    // Do not call again until the update completes (per update_done_yet()).
    virtual void update(
        uint32_t connector_id,
        DisplayMode const& mode,
        std::vector<DisplayLayer> const& layers  // Z-order, back to front
    ) = 0;

    // Returns {} if an update is still pending, otherwise returns status.
    virtual std::optional<DisplayUpdateDone> update_done_yet(uint32_t id) = 0;
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
// (The screen must be on a text console, not a desktop environment.)
// Each GPU may be opened *once* at a time across the *entire system*.
std::unique_ptr<DisplayDriver> open_display_driver(
    std::shared_ptr<UnixSystem> sys, std::string const& dev_file
);

// Debugging descriptions of structures. TODO: Add more?
std::string debug(DisplayDriverListing const&);
std::string debug(DisplayMode const&);

}  // namespace pivid
