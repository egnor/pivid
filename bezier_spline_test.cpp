#include "bezier_spline.h"

#include <limits>

#include <doctest/doctest.h>

#include "logging_policy.h"

namespace pivid {

TEST_CASE("BezierSpline::value") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .t = {1.0, 4.0},
        .begin_v = 10.0, .p1_v = 20.0, .p2_v = 30.0, .end_v = 40.0,
    });
    bz.segments.push_back({
        .t = {5.0, 8.0},
        .begin_v = 10.0, .p1_v = 30.0, .p2_v = 50.0, .end_v = 40.0,
    });
    bz.segments.push_back({
        .t = {11.0, INFINITY},
        .begin_v = 50.0, .p1_v = 60.0, .p2_v = 70.0, .end_v = 80.0,
    });

    SUBCASE("non-repeating") {
        CHECK_FALSE(bz.value(0.9));
        CHECK(*bz.value(1.0) == doctest::Approx(10.0));
        CHECK(*bz.value(1.1) == doctest::Approx(11.0));
        CHECK(*bz.value(2.5) == doctest::Approx(25.0));
        CHECK(*bz.value(3.9) == doctest::Approx(39.0));
        CHECK(*bz.value(4.0) == doctest::Approx(40.0));
        CHECK_FALSE(bz.value(4.1));

        CHECK_FALSE(bz.value(4.9));
        CHECK(*bz.value(5.0) == doctest::Approx(10.0));
        CHECK(*bz.value(5.1) == doctest::Approx(12.0).epsilon(0.01));
        CHECK(*bz.value(6.5) == doctest::Approx(36.25));
        CHECK(*bz.value(7.9) == doctest::Approx(40.9).epsilon(0.01));
        CHECK(*bz.value(8.0) == doctest::Approx(40.0));
        CHECK_FALSE(bz.value(8.1));

        CHECK_FALSE(bz.value(10.9));
        CHECK(*bz.value(11.0) == doctest::Approx(50.0));
        CHECK(*bz.value(11000000.0) == doctest::Approx(50.0));
    }

    SUBCASE("repeating") {
        bz.segments.resize(2);  // Drop infinite segment to allow repeat
        bz.repeat = true;

        CHECK(*bz.value(1.0) == doctest::Approx(10.0));
        CHECK(*bz.value(2.5) == doctest::Approx(25.0));
        CHECK(*bz.value(4.0) == doctest::Approx(40.0));
        CHECK_FALSE(bz.value(4.1));

        CHECK_FALSE(bz.value(4.9));
        CHECK(*bz.value(5.0) == doctest::Approx(10.0));
        CHECK(*bz.value(7.9) == doctest::Approx(40.9).epsilon(0.01));
        CHECK(*bz.value(8.0) == doctest::Approx(10.0));

        for (double t = 1.0; t < 6.0; t += 0.0999) {  // Avoid hairsplitting
            CAPTURE(t);
            if (t > 4.0 && t < 5.0) {
                CHECK_FALSE(bz.value(t - 7.0));
                CHECK_FALSE(bz.value(t - 14.0));
                CHECK_FALSE(bz.value(t));
                CHECK_FALSE(bz.value(t + 7.0));
                CHECK_FALSE(bz.value(t + 14.0));
            } else {
                double v = *bz.value(t);
                CHECK_FALSE(bz.value(t - 10.0));
                CHECK_FALSE(bz.value(t - 5.0));
                CHECK(bz.value(t + 7.0) == doctest::Approx(v));
                CHECK(bz.value(t + 14.0) == doctest::Approx(v));
            }
        }
    }
}

TEST_CASE("BezierSpline::range") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .t = {-2.0, 2.0},
        .begin_v = 10.0, .p1_v = -10.0, .p2_v = 50.0, .end_v = 40.0,
    });
    bz.segments.push_back({
        .t = {2.0, 6.0},
        .begin_v = 40.0, .p1_v = 30.0, .p2_v = 20.0, .end_v = 10.0,
    });

    Interval t;
    for (t.begin = -2.5; t.begin < 6.5; t.begin += 0.1999) {
        for (t.end = t.begin - 0.5; t.end < 7.0; t.end += 0.1999) {
            CAPTURE(t.begin);
            CAPTURE(t.end);

            auto range = bz.range(t);
            if (t.end < -2.0 || t.begin > 6.0 || t.empty()) {
                CHECK(range.empty());
            } else {
                REQUIRE(range.count() == 1);
                double min = 100, max = -100;
                for (double tt = t.begin; tt <= t.end; tt += 0.00999) {
                    auto const maybe_v = bz.value(tt);
                    if (maybe_v) {
                        min = std::min(min, *maybe_v);
                        max = std::max(max, *maybe_v);
                    }
                }

                auto const& mm = *range.begin();
                CHECK(mm.begin == doctest::Approx(min).epsilon(0.1));
                CHECK(mm.end == doctest::Approx(max).epsilon(0.1));
            }
        }
    }

    // TODO: Test range over infinite segments
    // TODO: Test range over repeating curves
}

TEST_CASE("BezierSplit::range of constant") {
    BezierSpline bz = {};
    bz.segments.push_back(constant_segment({1.5, 2.5}, 3.5));
    CHECK(bz.range({1.0, 1.5}).empty());
    CHECK(bz.range({2.0, 2.0}).empty());
    CHECK(bz.range({2.5, 3.0}).empty());

    auto const range = bz.range({1.5, 2.5});
    CHECK(range.count() == 1);
    auto const interval = *range.begin();
    CHECK(!interval.empty());
    CHECK(interval.begin == 3.5);
    CHECK(interval.end - interval.begin == doctest::Approx(0));
}

TEST_CASE("constant_segment") {
    auto bz = constant_segment({1.5, 2.5}, 3.5);
    CHECK(bz.t.begin == 1.5);
    CHECK(bz.t.end == 2.5);
    CHECK(bz.begin_v == 3.5);
    CHECK(bz.p1_v == 3.5);
    CHECK(bz.p2_v == 3.5);
    CHECK(bz.end_v == 3.5);
}

}  // namespace pivid
