#include "range_set.h"

#include <vector>

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("RangeSet add") {
    RangeSet<int> rset;
    std::vector<Range<int>> rvec(rset.begin(), rset.end());
    CHECK(rvec.size() == 0);

    rset.insert({10, 15});
    rvec.assign(rset.begin(), rset.end());
    REQUIRE(rvec.size() == 1);
    CHECK(rvec[0] == Range<int>(10, 15));

    SUBCASE("abutting") {
        rset.insert({15, 20});
        rset.insert({5, 10});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 1);
        CHECK(rvec[0] == Range<int>(5, 20));
    }

    SUBCASE("overlapping") {
        rset.insert({8, 13});
        rset.insert({12, 17});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 1);
        CHECK(rvec[0] == Range<int>(8, 17));
    }

    SUBCASE("distinct") {
        rset.insert({5, 7});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == Range<int>(5, 7));
        CHECK(rvec[1] == Range<int>(10, 15));

        rset.insert({18, 20});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 3);
        CHECK(rvec[0] == Range<int>(5, 7));
        CHECK(rvec[1] == Range<int>(10, 15));
        CHECK(rvec[2] == Range<int>(18, 20));

        SUBCASE("bridging") {
            rset.insert({7, 10});
            rset.insert({14, 21});
            rvec.assign(rset.begin(), rset.end());
            REQUIRE(rvec.size() == 1);
            CHECK(rvec[0] == Range<int>(5, 21));
        }
    }

    rset = {};

    SUBCASE("multiple bridging") {
        rset.insert({4, 6});
        rset.insert({4, 5});
        rset.insert({5, 6});
        rset.insert({9, 11});
        rset.insert({14, 16});
        rset.insert({19, 21});
        rset.insert({2, 15});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == Range<int>(2, 16));
        CHECK(rvec[1] == Range<int>(19, 21));
    }
}

TEST_CASE("RangeSet erase") {
    RangeSet<int> rset;
    rset.insert({5, 10});
    rset.insert({15, 20});
    rset.insert({25, 30});
    std::vector<Range<int>> rvec(rset.begin(), rset.end());
    REQUIRE(rvec.size() == 3);

    SUBCASE("erase identity") {
        rset.erase({15, 20});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == Range<int>(5, 10));
        CHECK(rvec[1] == Range<int>(25, 30));
    }

    SUBCASE("erase abutting") {
        rset.erase({10, 25});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == Range<int>(5, 10));
        CHECK(rvec[1] == Range<int>(25, 30));
    }

    SUBCASE("erase overlap") {
        rset.erase({7, 27});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 2);
        CHECK(rvec[0] == Range<int>(5, 7));
        CHECK(rvec[1] == Range<int>(27, 30));
    }

    SUBCASE("erase hole") {
        rset.erase({16, 18});
        rvec.assign(rset.begin(), rset.end());
        REQUIRE(rvec.size() == 4);
        CHECK(rvec[0] == Range<int>(5, 10));
        CHECK(rvec[1] == Range<int>(15, 16));
        CHECK(rvec[2] == Range<int>(18, 20));
        CHECK(rvec[3] == Range<int>(25, 30));
    }

    SUBCASE("erase all") {
        rset.erase({5, 30});
        rvec.assign(rset.begin(), rset.end());
        CHECK(rvec.size() == 0);
    }

    SUBCASE("erase beyond") {
        rset.erase({0, 35});
        rvec.assign(rset.begin(), rset.end());
        CHECK(rvec.size() == 0);
    }
}

TEST_CASE("RangeSet overlap") {
    RangeSet<int> rset;
    rset.insert({5, 10});
    rset.insert({15, 20});
    REQUIRE(std::distance(rset.begin(), rset.end()) == 2);

    SUBCASE("overlap_begin") {
        for (int i = 0; i < 10; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_begin(i) == rset.begin());
        }
        for (int i = 10; i < 20; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_begin(i) == std::next(rset.begin()));
        }
        for (int i = 20; i < 25; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_begin(i) == rset.end());
        }
    }

    SUBCASE("overlap end") {
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_end(i) == rset.begin());
        }
        for (int i = 6; i < 16; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_end(i) == std::next(rset.begin()));
        }
        for (int i = 16; i < 25; ++i) {
            CAPTURE(i);
            CHECK(rset.overlap_end(i) == rset.end());
        }
    }
}

TEST_CASE("RangeSet contains") {
    RangeSet<int> rset;
    rset.insert({5, 10});
    rset.insert({15, 20});

    CHECK_FALSE(rset.contains(4));
    CHECK(rset.contains(5));
    CHECK(rset.contains(6));
    CHECK(rset.contains(9));
    CHECK_FALSE(rset.contains(10));
    CHECK_FALSE(rset.contains(11));
    CHECK_FALSE(rset.contains(14));
    CHECK(rset.contains(15));
    CHECK(rset.contains(16));
    CHECK(rset.contains(19));
    CHECK_FALSE(rset.contains(20));
    CHECK_FALSE(rset.contains(21));
}

}  // namespace pivid
