#include "display_mode.h"

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("DisplayMode actual_hz") {
    DisplayMode mode = {};
    CHECK(mode.actual_hz() == 0);

    mode.nominal_hz = 1;  // Must be nonzero, otherwise not used.
    mode.pixel_khz = 123;
    mode.scan_size = {456, 789};
    CHECK(mode.actual_hz() == doctest::Approx(123000.0 / 456 / 789));
}

TEST_CASE("cta_861_modes") {
    CHECK(
        debug(cta_861_modes.front()) ==
        "  640x480p  @59.94  25.18M     16[ 96-]48  10[ 2-]33  4:3"
    );
    CHECK(
        debug(cta_861_modes.back()) ==
        " 4096x2160p @120     1188M     88[ 88+]128  8[10+]72  256:135"
    );
}

TEST_CASE("vesa_dmt_modes") {
    CHECK(
        debug(vesa_dmt_modes.front()) ==
        "  640x350p  @85.08   31.5M     32[ 64+]96  32[ 3-]60 "
    );
    CHECK(
        debug(vesa_dmt_modes.back()) ==
        " 4096x2160p @59.94  556.2M      8[ 32+]40  48[ 8-]6  "
    );
}

TEST_CASE("vesa_cvt_mode") {
    REQUIRE(vesa_cvt_mode({1280, 800}, 60));
    CHECK(
        debug(*vesa_cvt_mode({1280, 800}, 60)) ==
        " 1280x800p  @59.81   83.5M     72[128-]200  3[ 6+]22  16:10"
    );

    REQUIRE(vesa_cvt_mode({1400, 1050}, 85));
    CHECK(
        debug(*vesa_cvt_mode({1400, 1050}, 85)) ==
        " 1400x1050p @84.96  179.5M    104[152-]256  3[ 4+]48  4:3"
    );

    REQUIRE(vesa_cvt_mode({2560, 1600}, 75));
    CHECK(
        debug(*vesa_cvt_mode({2560, 1600}, 75)) ==
        " 2560x1600p @74.972 443.2M    208[280-]488  3[ 6+]63  16:10"
    );
}

TEST_CASE("vesa_cvt_rb_mode") {
    REQUIRE(vesa_cvt_rb_mode({1280, 800}, 60));
    CHECK(
        debug(*vesa_cvt_rb_mode({1280, 800}, 60)) ==
        " 1280x800p  @59.999 67.16M      8[ 32+]40   9[ 8-]6  "
    );

    REQUIRE(vesa_cvt_rb_mode({1400, 1050}, 85));
    CHECK(
        debug(*vesa_cvt_rb_mode({1400, 1050}, 85)) ==
        " 1400x1050p @85     137.5M      8[ 32+]40  29[ 8-]6  "
    );

    REQUIRE(vesa_cvt_rb_mode({2560, 1600}, 75));
    CHECK(
        debug(*vesa_cvt_rb_mode({2560, 1600}, 75)) ==
        " 2560x1600p @75     328.3M      8[ 32+]40  44[ 8-]6  "
    );

    REQUIRE(vesa_cvt_rb_mode({2560, 1600}, 120));
    CHECK(
        debug(*vesa_cvt_rb_mode({2560, 1600}, 120)) ==
        " 2560x1600p @120    536.7M      8[ 32+]40  80[ 8-]6  "
    );

    REQUIRE(vesa_cvt_rb_mode({1080, 1080}, 60));
    CHECK(
        debug(*vesa_cvt_rb_mode({1080, 1080}, 60)) ==
        " 1080x1080p @60     77.33M      8[ 32+]40  17[ 8-]6  "
    );
}

}  // namespace pivid
