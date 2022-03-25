#include "bezier_spline.h"

#include <limits>

#include <doctest/doctest.h>

#include "logging_policy.h"

namespace pivid {

TEST_CASE("bezier_value_at") {
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
        CHECK_FALSE(bezier_value_at(bz, 0.9));
        CHECK(*bezier_value_at(bz, 1.0) == doctest::Approx(10.0));
        CHECK(*bezier_value_at(bz, 1.1) == doctest::Approx(11.0));
        CHECK(*bezier_value_at(bz, 2.5) == doctest::Approx(25.0));
        CHECK(*bezier_value_at(bz, 3.9) == doctest::Approx(39.0));
        CHECK(*bezier_value_at(bz, 4.0) == doctest::Approx(40.0));
        CHECK_FALSE(bezier_value_at(bz, 4.1));

        CHECK_FALSE(bezier_value_at(bz, 4.9));
        CHECK(*bezier_value_at(bz, 5.0) == doctest::Approx(10.0));
        CHECK(*bezier_value_at(bz, 5.1) == doctest::Approx(12.0).epsilon(0.01));
        CHECK(*bezier_value_at(bz, 6.5) == doctest::Approx(36.25));
        CHECK(*bezier_value_at(bz, 7.9) == doctest::Approx(40.9).epsilon(0.01));
        CHECK(*bezier_value_at(bz, 8.0) == doctest::Approx(40.0));
        CHECK_FALSE(bezier_value_at(bz, 8.1));

        CHECK_FALSE(bezier_value_at(bz, 10.9));
        CHECK(*bezier_value_at(bz, 11.0) == doctest::Approx(50.0));
        CHECK(*bezier_value_at(bz, 11000000.0) == doctest::Approx(50.0));
    }

    SUBCASE("repeating") {
        bz.repeat = 5.0;

        CHECK(*bezier_value_at(bz, 1.0) == doctest::Approx(10.0));
        CHECK(*bezier_value_at(bz, 2.5) == doctest::Approx(25.0));
        CHECK(*bezier_value_at(bz, 4.0) == doctest::Approx(40.0));
        CHECK_FALSE(bezier_value_at(bz, 4.1));

        CHECK_FALSE(bezier_value_at(bz, 4.9));
        CHECK(*bezier_value_at(bz, 5.0) == doctest::Approx(10.0));
        CHECK(*bezier_value_at(bz, 5.9) == doctest::Approx(27.19));
        // Curve is interrupted by first repeat

        for (double t = 1.0; t < 6.0; t += 0.0999) {  // Avoid hairsplitting
            CAPTURE(t);
            if (t > 4.0 && t < 5.0) {
                CHECK_FALSE(bezier_value_at(bz, t - 10.0));
                CHECK_FALSE(bezier_value_at(bz, t - 5.0));
                CHECK_FALSE(bezier_value_at(bz, t));
                CHECK_FALSE(bezier_value_at(bz, t + 5.0));
                CHECK_FALSE(bezier_value_at(bz, t + 10.0));
            } else {
                double x = *bezier_value_at(bz, t);
                CHECK_FALSE(bezier_value_at(bz, t - 10.0));
                CHECK_FALSE(bezier_value_at(bz, t - 5.0));
                CHECK(bezier_value_at(bz, t + 5.0) == doctest::Approx(x));
                CHECK(bezier_value_at(bz, t + 10.0) == doctest::Approx(x));
            }
        }
    }
}

TEST_CASE("bezier_range_over") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .t = {-2.0, 2.0},
        .begin_x = 10.0, .p1_x = -10.0, .p2_x = 50.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .t = {2.0, 6.0},
        .begin_x = 40.0, .p1_x = 30.0, .p2_x = 20.0, .end_x = 10.0,
    });

    for (double t_begin = -2.5; t_begin < 6.5; t_begin += 0.1999) {
        for (double t_end = t_begin - 0.5; t_end < 7.0; t_end += 0.1999) {
            CAPTURE(t_begin);
            CAPTURE(t_end);

            auto minmax = bezier_range_over(bz, {t_begin, t_end});
            if (t_end < -2.0 || t_begin > 6.0 || t_end < t_begin) {
                CHECK(minmax.empty());
            } else {
                REQUIRE(minmax.count() == 1);
                double min = 100, max = -100;
                for (double t = t_begin; t <= t_end; t += 0.00999) {
                    auto const maybe_x = bezier_value_at(bz, t);
                    if (maybe_x) {
                        min = std::min(min, *maybe_x);
                        max = std::max(max, *maybe_x);
                    }
                }

                auto const& mm = *minmax.begin();
                CHECK(mm.begin == doctest::Approx(min).epsilon(0.1));
                CHECK(mm.end == doctest::Approx(max).epsilon(0.1));
            }
        }
    }

    // TODO: Test minmax over infinite segments
    // TODO: Test minmax over repeating curves
}

}  // namespace pivid
