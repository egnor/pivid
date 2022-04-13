#include "unix_system.h"

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("parse_realtime") {
    CHECK(
        parse_realtime("2022-04-12T17:06:03,086814454-07:00") ==
        doctest::Approx(1649808363.086814454)
    );

    CHECK(
        parse_realtime("2022-04-13T08:06:03.086814454+08:00") ==
        doctest::Approx(1649808363.086814454)
    );

    CHECK(
        parse_realtime("2022-04-13T00:06:03.086814454Z") ==
        doctest::Approx(1649808363.086814454)
    );

    CHECK(
        parse_realtime("2022-04-13T00:06:03.086814454") ==
        doctest::Approx(1649808363.086814454)
    );

    CHECK(parse_realtime("2022-04-13T00:06:03z") == 1649808363);
    CHECK(parse_realtime("2022-04-13 00:06:03") == 1649808363);
}

TEST_CASE("format_realtime") {
    CHECK(
        format_realtime(1649808363.086814454) == "2022-04-13 00:06:03.087Z"
    );
}

TEST_CASE("abbrev_realtime") {
    CHECK(abbrev_realtime(1649808363.086814454) == "00:06:03.087");
}

}  // namespace pivid
