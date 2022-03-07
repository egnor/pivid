#include "range_set.h"

#include <vector>

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("RangeSet add") {
    RangeSet<int> rset;
    std::vector<std::pair<int, int>> rvec(rset.begin(), rset.end());
    CHECK(rvec.size() == 0);

    rset.add({10, 15});
    rvec.assign(rset.begin(), rset.end());
    REQUIRE(rvec.size() == 1);
    CHECK(rvec[0] == std::make_pair(10, 15));

    SUBCASE("abutting") {
        rset.add({15, 20});
        rset.add({5, 10});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 1);
        CHECK(rvec[0] == std::make_pair(5, 20));
    }

    SUBCASE("overlapping") {
        rset.add({8, 13});
        rset.add({12, 17});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 1);
        CHECK(rvec[0] == std::make_pair(8, 17));
    }

    SUBCASE("distinct") {
        rset.add({5, 7});
        rset.add({18, 20});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 3);
        CHECK(rvec[0] == std::make_pair(5, 7));
        CHECK(rvec[1] == std::make_pair(10, 15));
        CHECK(rvec[2] == std::make_pair(18, 20));

        SUBCASE("bridging") {
            rset.add({7, 10});
            rset.add({14, 21});
            rvec.assign(rset.begin(), rset.end());
            REQUIRE(rvec.size() == 1);
            CHECK(rvec[0] == std::make_pair(5, 21));
        }
    }

    rset = {};

    SUBCASE("multiple bridging") {
        rset.add({4, 6});
        rset.add({4, 5});
        rset.add({5, 6});
        rset.add({9, 11});
        rset.add({14, 16});
        rset.add({19, 21});
        rset.add({2, 15});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == std::make_pair(2, 16));
        CHECK(rvec[1] == std::make_pair(19, 21));
    }
}

TEST_CASE("RangeSet remove") {
    RangeSet<int> rset;
    rset.add({5, 10});
    rset.add({15, 20});
    rset.add({25, 30});
    std::vector<std::pair<int, int>> rvec(rset.begin(), rset.end());
    REQUIRE(rvec.size() == 3);

    SUBCASE("remove identity") {
        rset.remove({15, 20});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == std::make_pair(5, 10));
        CHECK(rvec[1] == std::make_pair(25, 30));
    }

    SUBCASE("remove abutting") {
        rset.remove({10, 25});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == std::make_pair(5, 10));
        CHECK(rvec[1] == std::make_pair(25, 30));
    }

    SUBCASE("remove overlap") {
        rset.remove({7, 27});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == std::make_pair(5, 7));
        CHECK(rvec[1] == std::make_pair(27, 30));
    }

    SUBCASE("remove all") {
        rset.remove({5, 30});
        rvec.assign(rset.begin(), rset.end());
        CHECK(rvec.size() == 0);
    }

    SUBCASE("remove beyond") {
        rset.remove({0, 35});
        rvec.assign(rset.begin(), rset.end());
        CHECK(rvec.size() == 0);
    }
}

}  // namespace pivid
