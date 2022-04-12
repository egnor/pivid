#include "script_data.h"

#include <nlohmann/json.hpp>

#include <doctest/doctest.h>

using Approx = doctest::Approx;
using json = nlohmann::json;

namespace pivid {

TEST_CASE("from_json (empty)") {
    Script script;
    from_json(json::object(), script);

    CHECK(script.screens.empty());
    CHECK(script.standbys.empty());
    CHECK_FALSE(script.time_is_relative);
}

TEST_CASE("from_json") {
    auto const j = R"**({
      "main_loop_hz": 10.5,
      "main_buffer": 0.5,
      "screens": {
        "empty_screen": {},
        "full_screen": {
          "display_mode": [1920, 1080],
          "display_hz": 30,
          "update_hz": 15.5,
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
                  {"t": [0, 5], "v": [0.0, 1.0]},
                  {"t": [5, 10], "v": [1.0, 0.0]}
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
            "v": [0, 10, 90, 100],
            "repeat": 7200
          }
        }
      ]
    })**"_json;

    Script script;
    from_json(j, script);

    CHECK_FALSE(script.time_is_relative);
    CHECK(script.main_loop_hz == Approx(10.5));
    CHECK(script.main_buffer == Approx(0.5));

    REQUIRE(script.screens.size() == 2);
    REQUIRE(script.screens.count("empty_screen") == 1);
    CHECK(script.screens["empty_screen"].display_mode.x == 0);
    CHECK(script.screens["empty_screen"].display_mode.y == 0);
    CHECK(script.screens["empty_screen"].display_hz == 0);
    CHECK(script.screens["empty_screen"].update_hz == 0.0);
    CHECK(script.screens["empty_screen"].layers.empty());

    REQUIRE(script.screens.count("full_screen") == 1);
    auto const& screen = script.screens["full_screen"];
    CHECK(screen.display_mode.x == 1920);
    CHECK(screen.display_mode.y == 1080);
    CHECK(screen.display_hz == 30);
    CHECK(screen.update_hz == Approx(15.5));
    REQUIRE(screen.layers.size() == 2);
    CHECK(screen.layers[0].media.file == "empty_layer");
    CHECK(screen.layers[0].media.buffer == 0.2);
    CHECK(screen.layers[0].media.play.segments.size() == 1);  // Default
    CHECK(screen.layers[0].media.play.segments[0].t.begin == 0);
    CHECK(screen.layers[0].media.play.segments[0].t.end == 1e12);
    CHECK(screen.layers[0].media.play.segments[0].begin_v == 0);
    CHECK(screen.layers[0].media.play.segments[0].end_v == 0);

    CHECK(screen.layers[1].media.file == "full_layer");
    REQUIRE(screen.layers[1].media.play.segments.size() == 1);
    CHECK(screen.layers[1].media.play.repeat == 0.0);
    CHECK(screen.layers[1].media.play.segments[0].t.begin == 1);
    CHECK(screen.layers[1].media.play.segments[0].t.end == 1e12);
    CHECK(screen.layers[1].media.play.segments[0].begin_v == 0);
    CHECK(screen.layers[1].media.play.segments[0].p1_v == Approx(2e12 / 3));
    CHECK(screen.layers[1].media.play.segments[0].p2_v == Approx(2e12 * 2 / 3));
    CHECK(screen.layers[1].media.play.segments[0].end_v == 2 * (1e12 - 1));

    REQUIRE(screen.layers[1].from_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].from_size.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_xy.y.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.x.segments.size() == 1);
    REQUIRE(screen.layers[1].to_size.y.segments.size() == 1);

    CHECK(screen.layers[1].from_xy.x.segments[0].t.begin == 0);
    CHECK(screen.layers[1].from_xy.x.segments[0].t.end == 1e12);
    CHECK(screen.layers[1].from_xy.x.segments[0].begin_v == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p1_v == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].p2_v == 100);
    CHECK(screen.layers[1].from_xy.x.segments[0].end_v == 100);
    CHECK(screen.layers[1].from_xy.y.segments[0].begin_v == 200);
    CHECK(screen.layers[1].from_size.x.segments[0].begin_v == 300);
    CHECK(screen.layers[1].to_xy.x.segments[0].begin_v == 500);
    CHECK(screen.layers[1].to_size.x.segments[0].begin_v == 700);

    REQUIRE(screen.layers[1].opacity.segments.size() == 2);
    CHECK(screen.layers[1].opacity.repeat == 10.0);
    CHECK(screen.layers[1].opacity.segments[0].t.begin == 0);
    CHECK(screen.layers[1].opacity.segments[0].t.end == 5);
    CHECK(screen.layers[1].opacity.segments[0].begin_v == 0);
    CHECK(screen.layers[1].opacity.segments[0].p1_v == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].p2_v == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[0].end_v == 1);
    CHECK(screen.layers[1].opacity.segments[1].t.begin == 5);
    CHECK(screen.layers[1].opacity.segments[1].t.end == 10);
    CHECK(screen.layers[1].opacity.segments[1].begin_v == 1);
    CHECK(screen.layers[1].opacity.segments[1].p1_v == Approx(2.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].p2_v == Approx(1.0 / 3));
    CHECK(screen.layers[1].opacity.segments[1].end_v == 0);

    REQUIRE(script.standbys.size() == 1);
    auto const& standby = script.standbys[0];
    CHECK(standby.file == "standby");
    CHECK(standby.buffer == 0.5);

    REQUIRE(standby.play.segments.size() == 1);
    CHECK(standby.play.segments[0].t.begin == 1583064000);
    CHECK(standby.play.segments[0].t.end == Approx(1583064000 + 90.5));
    CHECK(standby.play.segments[0].begin_v == 0);
    CHECK(standby.play.segments[0].p1_v == 10);
    CHECK(standby.play.segments[0].p2_v == 90);
    CHECK(standby.play.segments[0].end_v == 100);
    CHECK(standby.play.repeat == 7200.0);
}

TEST_CASE("make_time_absolute") {
    auto const j = R"**({
      "time_is_relative": true,
      "screens": {
        "s0": {
          "layers": [
            {
              "media": {"file": "f0", "play": [{"t": [1, 2]}, {"t": [2, 3]}]},
              "from_xy": [{"t": [3, 4]}, {"t": [4, 5]}],
              "from_size": [5, 6],
              "to_xy": [6, 7],
              "to_size": [7, 8],
              "opacity": 9
            },
            {"media": {"file": "f1", "play": 10}}
          ]
        },
        "s1": {"layers": [{"media": {"file": "f2", "play": 11}}]}
      },
      "standbys": [{"file": "f3", "play": 12}, {"file": "f4", "play": 13}]
    })**"_json;

    Script script;
    from_json(j, script);
    CHECK(script.time_is_relative);
    CHECK(script.main_loop_hz == Script{}.main_loop_hz);
    CHECK(script.main_buffer == Script{}.main_buffer);

    fix_script_time(10000, &script);
    CHECK_FALSE(script.time_is_relative);
    REQUIRE(script.screens.size() == 2);
    REQUIRE(script.screens["s0"].layers.size() == 2);
    REQUIRE(script.screens["s1"].layers.size() == 1);

    auto const& s0_l0 = script.screens["s0"].layers[0];
    REQUIRE(s0_l0.media.play.segments.size() == 2);
    CHECK(s0_l0.media.play.segments[0].t.begin == Approx(10001));
    CHECK(s0_l0.media.play.segments[0].t.end == Approx(10002));
    CHECK(s0_l0.media.play.segments[1].t.begin == Approx(10002));
    CHECK(s0_l0.media.play.segments[1].t.end == Approx(10003));

    REQUIRE(s0_l0.from_xy.x.segments.size() == 1);
    REQUIRE(s0_l0.from_xy.y.segments.size() == 1);
    CHECK(s0_l0.from_xy.x.segments[0].t.begin == Approx(10003));
    CHECK(s0_l0.from_xy.y.segments[0].t.end == Approx(10005));

    REQUIRE(s0_l0.from_size.x.segments.size() == 1);
    CHECK(s0_l0.from_size.x.segments[0].t.begin == Approx(10000));
    CHECK(s0_l0.from_size.x.segments[0].t.end == Approx(1e12));
    CHECK(s0_l0.from_size.x.segments[0].begin_v == Approx(5));

    REQUIRE(s0_l0.to_xy.x.segments.size() == 1);
    REQUIRE(s0_l0.to_size.x.segments.size() == 1);
    REQUIRE(s0_l0.opacity.segments.size() == 1);
    CHECK(s0_l0.to_xy.x.segments[0].t.begin == Approx(10000));
    CHECK(s0_l0.to_size.x.segments[0].t.begin == Approx(10000));
    CHECK(s0_l0.opacity.segments[0].t.begin == Approx(10000));

    auto const& s0_l1 = script.screens["s0"].layers[1];
    REQUIRE(s0_l1.media.play.segments.size() == 1);
    CHECK(s0_l1.media.play.segments[0].t.begin == Approx(10000));
    CHECK(s0_l1.media.play.segments[0].begin_v == Approx(10));

    auto const& s1_l0 = script.screens["s1"].layers[0];
    REQUIRE(s1_l0.media.play.segments.size() == 1);
    CHECK(s1_l0.media.play.segments[0].t.begin == Approx(10000));
    CHECK(s1_l0.media.play.segments[0].begin_v == Approx(11));

    REQUIRE(script.standbys.size() == 2);
    REQUIRE(script.standbys[0].play.segments.size() == 1);
    CHECK(script.standbys[0].play.segments[0].t.begin == Approx(10000));
    CHECK(script.standbys[0].play.segments[0].begin_v == Approx(12));

    REQUIRE(script.standbys[1].play.segments.size() == 1);
    CHECK(script.standbys[1].play.segments[0].t.begin == Approx(10000));
    CHECK(script.standbys[1].play.segments[0].begin_v == Approx(13));
}

}  // namespace pivid
