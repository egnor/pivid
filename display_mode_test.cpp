#include "display_mode.h"

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("DisplayMode actual_hz") {
}

TEST_CASE("cta_861_modes") {
    CHECK(
        debug(cta_861_modes.front()) ==
        " 640x480p  @59.940  25.2M    16[ 96-]48  10[ 2-]33 4:3"
    );
    CHECK(
        debug(cta_861_modes.back()) ==
        "4096x2160p @120.000 1188.0M    88[ 88+]128  8[10+]72 256:135"
    );
}

TEST_CASE("vesa_dmt_modes") {
    CHECK(
        debug(vesa_dmt_modes.front()) ==
        " 640x350p  @85.080  31.5M    32[ 64+]96  32[ 3-]60"
    );
    CHECK(
        debug(vesa_dmt_modes.back()) ==
        "4096x2160p @59.940 556.2M     8[ 32+]40  48[ 8-]6 "
    );
}

TEST_CASE("vesa_cvt_mode") {
    REQUIRE(vesa_cvt_mode({1280, 800}, 60));
    CHECK(
         debug(*vesa_cvt_mode({1280, 800}, 60)) ==
         "1280x800p  @59.810  83.5M    72[128-]200  3[ 6+]22 16:10"
    );

    REQUIRE(vesa_cvt_mode({1400, 1050}, 85));
    CHECK(
         debug(*vesa_cvt_mode({1400, 1050}, 85)) ==
         "1400x1050p @84.960 179.5M   104[152-]256  3[ 4+]48 4:3"
    );

    REQUIRE(vesa_cvt_mode({2560, 1600}, 75));
    CHECK(
         debug(*vesa_cvt_mode({2560, 1600}, 75)) ==
         "2560x1600p @74.972 443.2M   208[280-]488  3[ 6+]63 16:10"
    );
}

}  // namespace pivid
