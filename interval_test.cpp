#include "interval.h"

#include <vector>

#include <doctest/doctest.h>

namespace pivid {

TEST_CASE("IntervalSet add") {
    IntervalSet iset;
    std::vector<Interval> ivec(iset.begin(), iset.end());
    CHECK(ivec.size() == 0);

    iset.insert({10, 15});
    ivec.assign(iset.begin(), iset.end());
    REQUIRE(ivec.size() == 1);
    CHECK(ivec[0] == Interval(10, 15));

    SUBCASE("abutting") {
        iset.insert({15, 20});
        iset.insert({5, 10});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 1);
        CHECK(ivec[0] == Interval(5, 20));
    }

    SUBCASE("overlapping") {
        iset.insert({8, 13});
        iset.insert({12, 17});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 1);
        CHECK(ivec[0] == Interval(8, 17));
    }

    SUBCASE("distinct") {
        iset.insert({5, 7});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 2);
        CHECK(ivec[0] == Interval(5, 7));
        CHECK(ivec[1] == Interval(10, 15));

        iset.insert({18, 20});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 3);
        CHECK(ivec[0] == Interval(5, 7));
        CHECK(ivec[1] == Interval(10, 15));
        CHECK(ivec[2] == Interval(18, 20));

        SUBCASE("bridging") {
            iset.insert({7, 10});
            iset.insert({14, 21});
            ivec.assign(iset.begin(), iset.end());
            REQUIRE(ivec.size() == 1);
            CHECK(ivec[0] == Interval(5, 21));
        }
    }

    iset = {};

    SUBCASE("multiple bridging") {
        iset.insert({4, 6});
        iset.insert({4, 5});
        iset.insert({5, 6});
        iset.insert({9, 11});
        iset.insert({14, 16});
        iset.insert({19, 21});
        iset.insert({2, 15});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 2);
        CHECK(ivec[0] == Interval(2, 16));
        CHECK(ivec[1] == Interval(19, 21));
    }
}

TEST_CASE("IntervalSet erase") {
    IntervalSet iset;
    iset.insert({5, 10});
    iset.insert({15, 20});
    iset.insert({25, 30});
    std::vector<Interval> ivec(iset.begin(), iset.end());
    REQUIRE(ivec.size() == 3);

    SUBCASE("erase identity") {
        iset.erase({15, 20});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 2);
        CHECK(ivec[0] == Interval(5, 10));
        CHECK(ivec[1] == Interval(25, 30));
    }

    SUBCASE("erase abutting") {
        iset.erase({10, 25});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 2);
        CHECK(ivec[0] == Interval(5, 10));
        CHECK(ivec[1] == Interval(25, 30));
    }

    SUBCASE("erase overlap") {
        iset.erase({7, 27});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 2);
        CHECK(ivec[0] == Interval(5, 7));
        CHECK(ivec[1] == Interval(27, 30));
    }

    SUBCASE("erase hole") {
        iset.erase({16, 18});
        ivec.assign(iset.begin(), iset.end());
        REQUIRE(ivec.size() == 4);
        CHECK(ivec[0] == Interval(5, 10));
        CHECK(ivec[1] == Interval(15, 16));
        CHECK(ivec[2] == Interval(18, 20));
        CHECK(ivec[3] == Interval(25, 30));
    }

    SUBCASE("erase all") {
        iset.erase({5, 30});
        ivec.assign(iset.begin(), iset.end());
        CHECK(ivec.size() == 0);
    }

    SUBCASE("erase beyond") {
        iset.erase({0, 35});
        ivec.assign(iset.begin(), iset.end());
        CHECK(ivec.size() == 0);
    }
}

TEST_CASE("IntervalSet overlap") {
    IntervalSet iset;
    iset.insert({5, 10});
    iset.insert({15, 20});
    REQUIRE(std::distance(iset.begin(), iset.end()) == 2);

    SUBCASE("overlap_begin") {
        for (int i = 0; i < 10; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_begin(i) == iset.begin());
        }
        for (int i = 10; i < 20; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_begin(i) == std::next(iset.begin()));
        }
        for (int i = 20; i < 25; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_begin(i) == iset.end());
        }
    }

    SUBCASE("overlap end") {
        for (int i = 0; i < 6; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_end(i) == iset.begin());
        }
        for (int i = 6; i < 16; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_end(i) == std::next(iset.begin()));
        }
        for (int i = 16; i < 25; ++i) {
            CAPTURE(i);
            CHECK(iset.overlap_end(i) == iset.end());
        }
    }
}

TEST_CASE("IntervalSet contains") {
    IntervalSet iset;
    iset.insert({5, 10});
    iset.insert({15, 20});

    CHECK_FALSE(iset.contains(4));
    CHECK(iset.contains(5));
    CHECK(iset.contains(6));
    CHECK(iset.contains(9));
    CHECK_FALSE(iset.contains(10));
    CHECK_FALSE(iset.contains(11));
    CHECK_FALSE(iset.contains(14));
    CHECK(iset.contains(15));
    CHECK(iset.contains(16));
    CHECK(iset.contains(19));
    CHECK_FALSE(iset.contains(20));
    CHECK_FALSE(iset.contains(21));
}

}  // namespace pivid
