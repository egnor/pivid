#include "script_data.h"

#include <nlohmann/json.hpp>

#include <doctest/doctest.h>

using Approx = doctest::Approx;
using json = nlohmann::json;

namespace pivid {

TEST_CASE("from_json") {
    Script script;
    from_json(json::object(), script);

    auto const j = R"**({
      "screens": {
        "empty_screen": {},
        "full_screen": {
          "mode": "1920x1080",
          "layers": [
            {"media": "empty_layer"},
            {
              "media": "full_layer",
              "time": {"t": 1, "rate": 2},
              "from_xy": [100, 200],
              "from_size": [300, 400],
              "to_xy": [500, 600],
              "to_size": [700, 800],
              "opacity": {
                "segments": [
                  {"t": [0, 5], "x": [0.0, 1.0]},
                  {"t": [5, 10], "x": [1.0, 0.0]}
                ],
                "repeat": 10.0
              }
            }
          ]
        }
      },
      "standby_layers": [
        {
          "media": "standby_layer",
          "time": {"t": [0, 100], "x": [0, 10, 90, 100]},
          "opacity": {"t": [0, 100], "x": [0, 100], "rate": [0.2, -0.1]}
        }
      ]
    })**"_json;
    from_json(j, script);

    REQUIRE(script.screens.size() == 2);
    REQUIRE(script.screens.count("empty_screen") == 1);
    CHECK(script.screens["empty_screen"].mode.empty());
    CHECK(script.screens["empty_screen"].layers.empty());

    REQUIRE(script.screens.count("full_screen") == 1);
    auto const& screen = script.screens["full_screen"];
    CHECK(screen.mode == "1920x1080");
    REQUIRE(screen.layers.size() == 2);
    CHECK(screen.layers[0].media == "empty_layer");
    CHECK(screen.layers[0].time.segments.empty());

    CHECK(screen.layers[1].media == "full_layer");
    REQUIRE(screen.layers[1].time.segments.size() == 1);
    CHECK(screen.layers[1].time.repeat == 0.0);
    CHECK(screen.layers[1].time.segments[0].begin_t == 1);
    CHECK(screen.layers[1].time.segments[0].end_t == 1e12 + 1);
    CHECK(screen.layers[1].time.segments[0].begin_x == 0);
    CHECK(screen.layers[1].time.segments[0].p1_x == Approx(2e12 / 3));
    CHECK(screen.layers[1].time.segments[0].p2_x == Approx(2e12 * 2 / 3));
    CHECK(screen.layers[1].time.segments[0].end_x == 2e12);

    REQUIRE(screen.layers[1].from_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.y.segments.size() == 1);

    CHECK(screen.layers[1].from_xy.x.segments[0].begin_t == 0);
    CHECK(screen.layers[1].from_xy.x.segments[0].end_t == 1e12);
    CHECK(screen.layers[1].from_xy.x.segments[0].begin_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p1_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p2_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].end_x == 100);
    CHECK(screen.layers[1].from_xy.y.segments[0].begin_x == 200);
    CHECK(screen.layers[1].from_size.x.segments[0].begin_x == 300);
    CHECK(screen.layers[1].to_xy.x.segments[0].begin_x == 500);
    CHECK(screen.layers[1].to_size.x.segments[0].begin_x == 700);

    REQUIRE(screen.layers[1].opacity.segments.size() == 2);
    CHECK(screen.layers[1].opacity.repeat == 10.0);
    CHECK(screen.layers[1].opacity.segments[0].begin_t == 0);
    CHECK(screen.layers[1].opacity.segments[0].end_t == 5);
    CHECK(screen.layers[1].opacity.segments[0].begin_x == 0);
    CHECK(screen.layers[1].opacity.segments[0].p1_x == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].p2_x == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].end_x == 1);
    CHECK(screen.layers[1].opacity.segments[1].begin_t == 5);
    CHECK(screen.layers[1].opacity.segments[1].end_t == 10);
    CHECK(screen.layers[1].opacity.segments[1].begin_x == 1);
    CHECK(screen.layers[1].opacity.segments[1].p1_x == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].p2_x == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].end_x == 0);

    REQUIRE(script.standby_layers.size() == 1);
    auto const& standby = script.standby_layers[0];
    CHECK(standby.media == "standby_layer");

    REQUIRE(standby.time.segments.size() == 1);
    CHECK(standby.time.segments[0].begin_t == 0);
    CHECK(standby.time.segments[0].end_t == 100);
    CHECK(standby.time.segments[0].begin_x == 0);
    CHECK(standby.time.segments[0].p1_x == 10);
    CHECK(standby.time.segments[0].p2_x == 90);
    CHECK(standby.time.segments[0].end_x == 100);

    CHECK(standby.from_xy.x.segments.empty());
    CHECK(standby.from_xy.y.segments.empty());
    CHECK(standby.from_size.x.segments.empty());
    CHECK(standby.from_size.y.segments.empty());
    CHECK(standby.to_xy.x.segments.empty());
    CHECK(standby.to_xy.y.segments.empty());
    CHECK(standby.to_size.x.segments.empty());
    CHECK(standby.to_size.y.segments.empty());

    REQUIRE(standby.opacity.segments.size() == 1);
    CHECK(standby.opacity.segments[0].begin_t == 0);
    CHECK(standby.opacity.segments[0].end_t == 100);
    CHECK(standby.opacity.segments[0].begin_x == 0);
    CHECK(standby.opacity.segments[0].p1_x == Approx(20.0 / 3));
    CHECK(standby.opacity.segments[0].p2_x == Approx(100.0 + 10.0 / 3));
    CHECK(standby.opacity.segments[0].end_x == 100);
}

}  // namespace pivid
