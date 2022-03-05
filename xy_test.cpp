#include "xy.h"

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("XY arithmetic") {
    CHECK(XY{3, 5}.x == 3);
    CHECK(XY{3, 5}.y == 5);

    CHECK(XY{1.1, 2.2}.as<int>() == XY{1, 2});

    CHECK(!XY{0, 0});
    CHECK(XY{1, 0});
    CHECK(XY{0, 1});
    CHECK(XY{3, 5} == XY{3, 5});
    CHECK(XY{3, 5} != XY{5, 3});
    CHECK(!(XY{3, 5} == XY{5, 3}));
    CHECK(!(XY{3, 5} != XY{3, 5}));

    CHECK(XY{3, 5} + XY{2, 1} == XY{5, 6});
    CHECK(XY{3, 5} - XY{2, 1} == XY{1, 4});
    CHECK(-XY{3, 5} == XY{-3, -5});
    CHECK(XY{3, 5} * 2 == XY{6, 10});
    CHECK(XY{3, 5} / 2 == XY{1, 2});
}

}  // namespace pivid
