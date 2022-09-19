// Interfaces to display and overlay images on-screen.

#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <optional>
#include <vector>

#include "display_mode.h"
#include "image_buffer.h"
#include "unix_system.h"
#include "xy.h"

namespace pivid {

// Current screen state and recommended modes based on monitor data (EDID).
// Returned by scan_screens().
struct DisplayScreen {
    uint32_t id = 0;
    std::string connector;           // Like "HDMI-1"
    bool display_detected = false;   // True if a monitor is connected
    DisplayMode active_mode;
    std::vector<DisplayMode> modes;  // First mode is the "best".
    // TODO: Add screen name from EDID, if known?
};

// Where one image (or a portion thereof) should be shown on screen
struct DisplayLayer {
    std::shared_ptr<LoadedImage> image;  // From DisplayDriver::load_image()
    XY<double> from_xy = {}, from_size = {};
    XY<int> to_xy = {}, to_size = {};
    double opacity = 1.0;
    bool reflect = false;  // Horizontal reflection applied before any rotation
    int rotate = 0;        // Clockwise rotation: 0, 90, 180, 270
};

// A complete description of what to show on screen
struct DisplayFrame {
    DisplayMode mode;
    std::vector<DisplayLayer> layers;        // Ordered from back to front
    std::vector<std::string> warnings = {};  // Log if this frame is shown
};

// Returned by DisplayDriver::update() after a frame has become visible.
struct DisplayUpdated {
    double flip_time;                      // Time of vsync flip
    std::optional<ImageBuffer> writeback;  // Output for writeback "connectors"
};

// Estimate of display load factors, where 1.0 is max capacity.
struct DisplayCost {
    double memory_bandwidth = 0.0;
    double compositor_bandwidth = 0.0;
    double line_buffer_memory = 0.0;
};

// Interface to a GPU device. Normally one per system, handling all outputs.
// Returned by open_display_driver().
// *Internally synchronized* for multithreaded access.
class DisplayDriver {
  public:
    virtual ~DisplayDriver() = default;

    // Returns the ID, name, and current status of all video connectors.
    virtual std::vector<DisplayScreen> scan_screens() = 0;

    // Imports an image into the GPU for use in DisplayUpdateRequest.
    virtual std::unique_ptr<LoadedImage> load_image(ImageBuffer) = 0;

    // Updates a screen's contents &/or video mode at vsync.
    // BLOCKS until the vsync has occurred and the update is complete.
    virtual DisplayUpdated update(uint32_t screen_id, DisplayFrame const&) = 0;

    // Estimate the system load needed to show a particular frame.
    virtual DisplayCost predict_cost(DisplayFrame const&) const = 0;
};

// Description of a GPU device. Returned by list_device_drivers().
struct DisplayDriverListing {
    std::string dev_file;       // Like "/dev/dri/card0"
    std::string system_path;    // Like "platform/gpu/drm/card0" (more stable)
    std::string driver;         // Like "vc4" or "i915"
    std::string driver_date;    // Like "20140616" (first development date)
    std::string driver_desc;    // Like "Broadcom VC4 graphics"
    std::string driver_bus_id;  // Like "fec00000.v3d" (PCI address, etc)
    auto operator<=>(DisplayDriverListing const&) const = default;
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

// Debugging descriptions of structures.
std::string debug(DisplayLayer const&);
std::string debug(DisplayDriverListing const&);

}  // namespace pivid
