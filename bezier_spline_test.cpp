#include "bezier_spline.h"

#include <limits>

#include <doctest/doctest.h>

#include "logging_policy.h"

namespace pivid {

TEST_CASE("BezierSpline::value") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .t = {1.0, 4.0},
        .begin_x = 10.0, .p1_x = 20.0, .p2_x = 30.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .t = {5.0, 8.0},
        .begin_x = 10.0, .p1_x = 30.0, .p2_x = 50.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .t = {11.0, std::numeric_limits<double>::infinity()},
        .begin_x = 50.0, .p1_x = 60.0, .p2_x = 70.0, .end_x = 80.0,
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
                double x = *bz.value(t);
                CHECK_FALSE(bz.value(t - 10.0));
                CHECK_FALSE(bz.value(t - 5.0));
                CHECK(bz.value(t + 7.0) == doctest::Approx(x));
                CHECK(bz.value(t + 14.0) == doctest::Approx(x));
            }
        }
    }
}

TEST_CASE("BezierSpline::range") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .t = {-2.0, 2.0},
        .begin_x = 10.0, .p1_x = -10.0, .p2_x = 50.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .t = {2.0, 6.0},
        .begin_x = 40.0, .p1_x = 30.0, .p2_x = 20.0, .end_x = 10.0,
    });

    Interval<double> t;
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
                    auto const maybe_x = bz.value(tt);
                    if (maybe_x) {
                        min = std::min(min, *maybe_x);
                        max = std::max(max, *maybe_x);
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

}  // namespace pivid
