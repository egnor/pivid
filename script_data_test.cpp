#include "script_data.h"

#include <nlohmann/json.hpp>

#include <doctest/doctest.h>

using Approx = doctest::Approx;
using json = nlohmann::json;

namespace pivid {

TEST_CASE("from_json (empty)") {
    Script script = parse_script("{}", 123.45);
    CHECK(script.media.empty());
    CHECK(script.screens.empty());
    CHECK(script.zero_time == 123.45);
}

TEST_CASE("from_json") {
    auto const text = R"**({
      "main_loop_hz": 10.5,
      "main_buffer_time": 0.5,
      "zero_time": 12345.678,
      "screens": {
        "empty_screen": {},
        "full_screen": {
          "display_mode": [1920, 1080],
          "display_hz": 30,
          "update_hz": 15.5,
          "layers": [
            {"media": "empty_layer"},
            {
              "media": "full_layer",
              "play": {"t": 1, "rate": 2},
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
      "media": {
        "media1": {
          "preload": 1.1,
          "decoder_idle_time": 1.5,
          "seek_scan_time": 2.5
        },
        "media2": {
          "preload": [
            [2.1, 2.2],
            [{"t": [1, 5], "v": [2.3, 2.4]}, 2.5]
          ]
        },
        "media3": {"preload": [3.1, 3.2]}
      }
    })**";

    Script script = parse_script(text, 876.54321);
    CHECK(script.main_loop_hz == Approx(10.5));
    CHECK(script.main_buffer_time == Approx(0.5));
    CHECK(script.zero_time == 12345.678);

    REQUIRE(script.media.size() == 3);
    REQUIRE(script.media.count("media1") == 1);
    auto const& media1 = script.media["media1"];
    REQUIRE(media1.preload.size() == 1);
    REQUIRE(media1.preload[0].begin.segments.size() == 1);
    REQUIRE(media1.preload[0].end.segments.size() == 1);
    CHECK(media1.preload[0].begin.segments[0].t.begin == 0);
    CHECK(media1.preload[0].begin.segments[0].t.end == 1e12);
    CHECK(media1.preload[0].begin.segments[0].begin_v == 0);
    CHECK(media1.preload[0].begin.segments[0].end_v == 0);
    CHECK(media1.preload[0].end.segments[0].t.begin == 0);
    CHECK(media1.preload[0].end.segments[0].t.end == 1e12);
    CHECK(media1.preload[0].end.segments[0].begin_v == 1.1);
    CHECK(media1.preload[0].end.segments[0].end_v == 1.1);
    CHECK(media1.decoder_idle_time == 1.5);
    CHECK(media1.seek_scan_time == 2.5);

    REQUIRE(script.media.count("media2") == 1);
    auto const& media2 = script.media["media2"];
    REQUIRE(media2.preload.size() == 2);
    REQUIRE(media2.preload[0].begin.segments.size() == 1);
    REQUIRE(media2.preload[0].end.segments.size() == 1);
    REQUIRE(media2.preload[1].begin.segments.size() == 1);
    REQUIRE(media2.preload[1].end.segments.size() == 1);
    CHECK(media2.preload[0].begin.segments[0].begin_v == 2.1);
    CHECK(media2.preload[0].end.segments[0].begin_v == 2.2);
    CHECK(media2.preload[1].begin.segments[0].t.begin == 1);
    CHECK(media2.preload[1].begin.segments[0].t.end == 5);
    CHECK(media2.preload[1].begin.segments[0].begin_v == 2.3);
    CHECK(media2.preload[1].begin.segments[0].end_v == 2.4);
    CHECK(media2.preload[1].end.segments[0].begin_v == 2.5);
    CHECK(media2.preload[1].end.segments[0].end_v == 2.5);

    REQUIRE(script.media.count("media3") == 1);
    auto const& media3 = script.media["media3"];
    REQUIRE(media3.preload.size() == 1);
    REQUIRE(media3.preload[0].begin.segments.size() == 1);
    REQUIRE(media3.preload[0].end.segments.size() == 1);
    CHECK(media3.preload[0].begin.segments[0].begin_v == 3.1);
    CHECK(media3.preload[0].end.segments[0].begin_v == 3.2);

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
    CHECK(screen.layers[0].media == "empty_layer");
    CHECK(screen.layers[0].play.segments.size() == 1);  // Default
    CHECK(screen.layers[0].play.segments[0].t.begin == 0);
    CHECK(screen.layers[0].play.segments[0].t.end == 1e12);
    CHECK(screen.layers[0].play.segments[0].begin_v == 0);
    CHECK(screen.layers[0].play.segments[0].end_v == 0);
    CHECK(screen.layers[0].buffer == 0.2);

    CHECK(screen.layers[1].media == "full_layer");
    REQUIRE(screen.layers[1].play.segments.size() == 1);
    CHECK(screen.layers[1].play.repeat == 0.0);
    CHECK(screen.layers[1].play.segments[0].t.begin == 1);
    CHECK(screen.layers[1].play.segments[0].t.end == 1e12);
    CHECK(screen.layers[1].play.segments[0].begin_v == 0);
    CHECK(screen.layers[1].play.segments[0].p1_v == Approx(2e12 / 3));
    CHECK(screen.layers[1].play.segments[0].p2_v == Approx(2e12 * 2 / 3));
    CHECK(screen.layers[1].play.segments[0].end_v == 2 * (1e12 - 1));

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
}

}  // namespace pivid
