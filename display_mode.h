// Functions to access a database of standard display modes.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "xy.h"

namespace pivid {

// Description of video mode resolution & timings (like XFree86 Modeline).
// Available modes come from DisplayDriver::scan_screens();
// the desired mode to use is given to DisplayDriver::request_update().
// (A custom/tweaked mode may also be used if you're wild and crazy.)
struct DisplayMode {
    XY<int> size;           // Displayable pixel size
    XY<int> scan_size;      // Overall timing size
    XY<int> sync_start;     // Horiz / vert sync start
    XY<int> sync_end;       // Horiz / vert sync pulse end
    XY<int> sync_polarity;  // Horiz / vert sync polarity (+1 / -1)
    XY<int> doubling;       // Clock doubling / doublescan / interlace (+1 / -1)
    XY<int> aspect;         // Picture aspect ratio (0/0 if unspecified)
    int pixel_khz = 0;      // Basic pixel clock
    int nominal_hz = 0;     // Approx refresh rate (like 30 or 60)
    double actual_hz() const;  // Computes true refresh frequency
};

// All modes listed in the CTA-861 standard
extern std::vector<DisplayMode> const cta_861_modes;

// Returns all modes listed in the VESA DMT standard
extern std::vector<DisplayMode> const vesa_dmt_modes;

// Generates a mode compliant with the VESA CVT standard, if possible
// TODO: Allow specification of Reduced Blanking (RB) modes?
std::optional<DisplayMode> vesa_cvt_mode(XY<int> size, int hz);

// Debugging description of DisplayMode.
std::string debug(DisplayMode const&);

}  // namespace pivid
