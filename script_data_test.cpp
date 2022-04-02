#include "script_data.h"

#include <nlohmann/json.hpp>

#include <doctest/doctest.h>

using Approx = doctest::Approx;
using json = nlohmann::json;

namespace pivid {

TEST_CASE("from_json") {
    auto const j = R"**({
      "screens": {
        "empty_screen": {},
        "full_screen": {
          "mode": [1920, 1080],
          "mode_hz": 30,
          "layers": [
            {"media": {"file": "empty_layer"}},
            {
              "media": {"file": "full_layer", "play": {"t": 1, "rate": 2}},
              "from_xy": [100, 200],
              "from_size": [300, 400],
              "to_xy": [500, 600],
              "to_size": [700, 800],
              "opacity": {
                "segments": [
                  {"t": [0, 5], "x": [0.0, 1.0]},
                  {"t": [5, 10], "x": [1.0, 0.0]}
                ],
                "repeat": true
              }
            }
          ]
        }
      },
      "standbys": [
        {
          "file": "standby",
          "buffer": 0.5,
          "play": {
            "t": ["2020-03-01T12:00:00Z", "2020-03-01T12:01:30.5Z"],
            "x": [0, 10, 90, 100]
          }
        }
      ]
    })**"_json;

    Script script;
    from_json(j, script, 1e10);  // Not run_relative; run_start is ignored.

    REQUIRE(script.screens.size() == 2);
    REQUIRE(script.screens.count("empty_screen") == 1);
    CHECK(script.screens["empty_screen"].mode.x == 0);
    CHECK(script.screens["empty_screen"].mode.y == 0);
    CHECK(script.screens["empty_screen"].mode_hz == 0);
    CHECK(script.screens["empty_screen"].layers.empty());

    REQUIRE(script.screens.count("full_screen") == 1);
    auto const& screen = script.screens["full_screen"];
    CHECK(screen.mode.x == 1920);
    CHECK(screen.mode.y == 1080);
    CHECK(screen.mode_hz == 30);
    REQUIRE(screen.layers.size() == 2);
    CHECK(screen.layers[0].media.file == "empty_layer");
    CHECK(screen.layers[0].media.play.segments.empty());
    CHECK(screen.layers[0].media.buffer == 0.2);

    CHECK(screen.layers[1].media.file == "full_layer");
    REQUIRE(screen.layers[1].media.play.segments.size() == 1);
    CHECK_FALSE(screen.layers[1].media.play.repeat);
    CHECK(screen.layers[1].media.play.segments[0].t.begin == 1);
    CHECK(screen.layers[1].media.play.segments[0].t.end == 1e12);
    CHECK(screen.layers[1].media.play.segments[0].begin_x == 0);
    CHECK(screen.layers[1].media.play.segments[0].p1_x == Approx(2e12 / 3));
    CHECK(screen.layers[1].media.play.segments[0].p2_x == Approx(2e12 * 2 / 3));
    CHECK(screen.layers[1].media.play.segments[0].end_x == 2 * (1e12 - 1));

    REQUIRE(screen.layers[1].from_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.y.segments.size() == 1);

    CHECK(screen.layers[1].from_xy.x.segments[0].t.begin == -1e12);
    CHECK(screen.layers[1].from_xy.x.segments[0].t.end == 1e12);
    CHECK(screen.layers[1].from_xy.x.segments[0].begin_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p1_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p2_x == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].end_x == 100);
    CHECK(screen.layers[1].from_xy.y.segments[0].begin_x == 200);
    CHECK(screen.layers[1].from_size.x.segments[0].begin_x == 300);
    CHECK(screen.layers[1].to_xy.x.segments[0].begin_x == 500);
    CHECK(screen.layers[1].to_size.x.segments[0].begin_x == 700);

    REQUIRE(screen.layers[1].opacity.segments.size() == 2);
    CHECK(screen.layers[1].opacity.repeat);
    CHECK(screen.layers[1].opacity.segments[0].t.begin == 0);
    CHECK(screen.layers[1].opacity.segments[0].t.end == 5);
    CHECK(screen.layers[1].opacity.segments[0].begin_x == 0);
    CHECK(screen.layers[1].opacity.segments[0].p1_x == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].p2_x == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].end_x == 1);
    CHECK(screen.layers[1].opacity.segments[1].t.begin == 5);
    CHECK(screen.layers[1].opacity.segments[1].t.end == 10);
    CHECK(screen.layers[1].opacity.segments[1].begin_x == 1);
    CHECK(screen.layers[1].opacity.segments[1].p1_x == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].p2_x == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].end_x == 0);

    REQUIRE(script.standbys.size() == 1);
    auto const& standby = script.standbys[0];
    CHECK(standby.file == "standby");
    CHECK(standby.buffer == 0.5);

    REQUIRE(standby.play.segments.size() == 1);
    CHECK(standby.play.segments[0].t.begin == 1583064000);
    CHECK(standby.play.segments[0].t.end == Approx(1583064000 + 90.5));
    CHECK(standby.play.segments[0].begin_x == 0);
    CHECK(standby.play.segments[0].p1_x == 10);
    CHECK(standby.play.segments[0].p2_x == 90);
    CHECK(standby.play.segments[0].end_x == 100);
    CHECK_FALSE(standby.play.repeat);
}

TEST_CASE("from_json (empty)") {
    Script script;
    from_json(json::object(), script);

    CHECK(script.screens.empty());
    CHECK(script.standbys.empty());
    CHECK_FALSE(script.run_relative);
}

TEST_CASE("from_json (run_relative=true)") {
    auto const j = R"**({
      "run_relative": true,
      "screens": {
        "screen": {
          "layers": [
            {
              "media": {"file": "full_layer", "play": {"t": 1, "rate": 2}},
              "from_xy": [100, 200],
              "from_size": [300, 400],
              "to_xy": [500, 600],
              "to_size": [700, 800],
              "opacity": {
                "segments": [
                  {"t": [0, 5], "x": [0.0, 1.0]},
                  {"t": [5, 10], "x": [1.0, 0.0]}
                ],
                "repeat": true
              }
            }
          ]
        }
      },
      "standbys": [
        {
          "file": "standby",
          "buffer": 0.5,
          "play": {
            "t": ["2020-03-01T12:00:00Z", "2020-03-01T12:01:30.5Z"],
            "x": [0, 10, 90, 100]
          }
        }
      ]
    })**"_json;

    Script script;
    from_json(j, script, 1e9);  // run_start will be added to times
}

}  // namespace pivid
