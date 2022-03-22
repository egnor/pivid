#include "bezier_spline.h"

#include <limits>

#include <doctest/doctest.h>

#include "logging_policy.h"

namespace pivid {

TEST_CASE("bezier_value_at") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .begin_t = 1.0, .end_t = 4.0,
        .begin_x = 10.0, .p1_x = 20.0, .p2_x = 30.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .begin_t = 5.0, .end_t = 8.0,
        .begin_x = 10.0, .p1_x = 30.0, .p2_x = 50.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .begin_t = 11.0, .end_t = std::numeric_limits<double>::infinity(),
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

TEST_CASE("bezier_minmax_over") {
    BezierSpline bz = {};
    bz.segments.push_back({
        .begin_t = -2.0, .end_t = 2.0,
        .begin_x = 10.0, .p1_x = -10.0, .p2_x = 50.0, .end_x = 40.0,
    });
    bz.segments.push_back({
        .begin_t = 2.0, .end_t = 6.0,
        .begin_x = 40.0, .p1_x = 30.0, .p2_x = 20.0, .end_x = 10.0,
    });

    for (double from_t = -2.5; from_t < 6.5; from_t += 0.1999) {
        for (double to_t = from_t - 0.5; to_t < 7.0; to_t += 0.1999) {
            CAPTURE(from_t);
            CAPTURE(to_t);

            auto minmax = bezier_minmax_over(bz, from_t, to_t);
            if (to_t < -2.0 || from_t > 6.0 || to_t < from_t) {
                CHECK(minmax.empty());
            } else {
                REQUIRE(minmax.size() == 1);
                double min = 100, max = -100;
                for (double t = from_t; t <= to_t; t += 0.00999) {
                    auto const maybe_x = bezier_value_at(bz, t);
                    if (maybe_x) {
                        min = std::min(min, *maybe_x);
                        max = std::max(max, *maybe_x);
                    }
                }

                auto const& mm = minmax[0];
                CHECK(mm.first == doctest::Approx(min).epsilon(0.1));
                CHECK(mm.second == doctest::Approx(max).epsilon(0.1));
            }
        }
    }

    // TODO: Test minmax over infinite segments
    // TODO: Test minmax over repeating curves
}

}  // namespace pivid
