// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>

extern "C" {
#include <libavutil/log.h>
}

#include "display_output.h"
#include "frame_player.h"
#include "logging_policy.h"
#include "media_decoder.h"

namespace pivid {

namespace {

std::shared_ptr<spdlog::logger> const& main_logger() {
    static const auto logger = make_logger("main");
    return logger;
}

std::unique_ptr<DisplayDriver> find_driver(std::string const& dev_arg) {
    if (dev_arg == "none" || dev_arg == "/dev/null") return {};

    fmt::print("=== Video drivers ===\n");
    auto const sys = global_system();
    std::string found;
    for (auto const& d : list_display_drivers(sys)) {
        if (
            found.empty() && (
                d.dev_file.find(dev_arg) != std::string::npos ||
                d.system_path.find(dev_arg) != std::string::npos ||
                d.driver.find(dev_arg) != std::string::npos ||
                d.driver_bus_id.find(dev_arg) != std::string::npos
            )
        ) {
            found = d.dev_file;
        }
        fmt::print("{} {}\n", (found == d.dev_file) ? "=>" : "  ", debug(d));
    }
    fmt::print("\n");

    if (found.empty()) throw std::runtime_error("No matching device");
    return open_display_driver(sys, found);
}

DisplayConnector find_connector(
    std::unique_ptr<DisplayDriver> const& driver, std::string const& conn_arg
) {
    if (!driver) return {};

    fmt::print("=== Video display connectors ===\n");
    DisplayConnector found = {};
    for (auto const& conn : driver->scan_connectors()) {
        if (found.name.empty() && conn.name.find(conn_arg) != std::string::npos)
            found = conn;

        fmt::print(
            "{} Conn #{:<3} {}{}\n",
            found.id == conn.id ? "=>" : "  ", conn.id, conn.name,
            conn.display_detected ? " [connected]" : " [no connection]"
        );
    }
    fmt::print("\n");

    if (!found.id) throw std::runtime_error("No matching connector");
    return found;
}

DisplayMode find_mode(
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnector const& conn,
    std::string const& mode_arg
) {
    if (!driver || !conn.id) return {};

    fmt::print("=== Video modes ===\n");
    std::set<std::string> seen;
    DisplayMode found = mode_arg.empty() ? conn.active_mode : DisplayMode{};
    for (auto const& mode : conn.modes) {
        std::string const mode_str = debug(mode);
        if (found.name.empty() && mode_str.find(mode_arg) != std::string::npos)
            found = mode;

        if (seen.insert(mode.name).second) {
            fmt::print(
                "{} {}{}\n",
                found.name == mode.name ? "=>" : "  ", mode_str,
                conn.active_mode.name == mode.name ? " [on]" : ""
            );
        }
    }
    fmt::print("\n");

    if (found.name.empty()) throw std::runtime_error("No matching mode");
    return found;
}

std::unique_ptr<MediaDecoder> find_media(std::string const& media_arg) {
    if (media_arg.empty()) return {};

    fmt::print("=== Playing media ({}) ===\n", media_arg);
    auto decoder = open_media_decoder(media_arg);
    return decoder;
}

void play_video(
    std::unique_ptr<MediaDecoder> const& decoder,
    std::unique_ptr<MediaDecoder> const& overlay,
    std::string const& tiff_arg,
    std::unique_ptr<DisplayDriver> const& driver,
    DisplayConnector const& conn,
    DisplayMode const& mode
) {
    using namespace std::chrono_literals;
    auto const logger = main_logger();
    auto const sys = global_system();

    DisplayLayer overlay_layer = {};
    if (overlay) {
        logger->trace("Loading overlay image...");
        std::optional<MediaFrame> frame = overlay->next_frame();
        if (!frame)
            throw std::runtime_error("No frames in overlay media");

        overlay_layer.image = driver->load_image(frame->image);
        overlay_layer.from_size = frame->image.size.as<double>();
        overlay_layer.to = (mode.size - frame->image.size) / 2;
        overlay_layer.to_size = frame->image.size;
    }

    FramePlayer::Timeline timeline;
    std::unique_ptr<FramePlayer> player;
    if (driver)
        player = start_frame_player(sys, driver.get(), conn.id, mode);

    int frame_index = 0;
    std::optional<FramePlayer::Timeline::key_type> start_time;
    logger->trace("Getting first video frame...");
    while (decoder) {
        if (player) {
            auto const last_shown = player->last_shown();
            while (!timeline.empty() && timeline.begin()->first < last_shown)
                timeline.erase(timeline.begin());
            if (timeline.size() >= 10) {
                logger->trace("> (sleeping for playback...)");
                sys->wait_until(sys->steady_time() + 50ms);
                continue;
            }
        }

        auto const media_frame = decoder->next_frame();
        if (!media_frame) break;

        if (!start_time) {
            start_time = sys->steady_time() - media_frame->time;
            logger->debug(
                "Play start @ {:.3f}", start_time->time_since_epoch() / 1.0s
            );
        }

        if (player) {
            std::vector<DisplayLayer> layers;

            DisplayLayer layer = {};
            layer.image = driver->load_image(media_frame->image);
            layer.from_size = media_frame->image.size.as<double>();
            layer.to_size = mode.size;
            layers.push_back(std::move(layer));

            if (overlay_layer.image)
                layers.push_back(overlay_layer);

            auto const play_time = *start_time + media_frame->time;
            timeline[play_time] = layers;
            if (logger->should_log(log_level::debug)) {
                logger->debug(
                    "Adding frame @ {:.3f}",
                    play_time.time_since_epoch() / 1.0s
                );
            }

            player->set_timeline(timeline);
        }

        if (!tiff_arg.empty()) {
            auto dot_pos = tiff_arg.rfind('.');
            if (dot_pos == std::string::npos) dot_pos = tiff_arg.size();

            logger->trace("Encoding TIFF...");
            auto path = tiff_arg.substr(0, dot_pos);
            path += fmt::format(".F{:05d}", frame_index);
            path += tiff_arg.substr(dot_pos);
            auto tiff = debug_tiff(media_frame->image);

            logger->trace("Writing TIFF...");
            std::ofstream ofs;
            ofs.exceptions(~std::ofstream::goodbit);
            ofs.open(path, std::ios::binary);
            ofs.write((char const*) tiff.data(), tiff.size());
            logger->trace("Saving: {}", path);
        }

        ++frame_index;
        logger->trace("Getting next video frame (#{})...", frame_index);
    }

    logger->debug("End of media file reached");

    while (
        player && !timeline.empty() &&
        player->last_shown() < timeline.rbegin()->first
    ) {
        auto const left = timeline.rbegin()->first - player->last_shown();
        logger->trace("Sleeping for final playback ({}ms left)...", left / 1ms);
        sys->wait_until(sys->steady_time() + 50ms);
    }
    player.reset();

    logger->debug("End of playback");
}

}  // namespace

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg;
    std::string conn_arg;
    std::string log_arg;
    std::string mode_arg;
    std::string media_arg;
    std::string overlay_arg;
    double seek_arg = 0.0;
    std::string tiff_arg;
    bool debug_libav = false;
    double sleep_arg = 0.0;

    CLI::App app("Decode and show a media file");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--connector", conn_arg, "Video output");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--mode", mode_arg, "Video mode");
    app.add_option("--media", media_arg, "Media file to play");
    app.add_option("--overlay", overlay_arg, "Image file to overlay");
    app.add_option("--seek", seek_arg, "Seek this many seconds into media");
    app.add_option("--sleep", sleep_arg, "Wait this long before exiting");
    app.add_option("--save_tiff", tiff_arg, "Save frames as .tiff");
    app.add_flag("--debug_libav", debug_libav, "Enable libav* debug logs");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    if (debug_libav) av_log_set_level(AV_LOG_DEBUG);

    try {
        auto const driver = find_driver(dev_arg);
        auto const conn = find_connector(driver, conn_arg);
        auto const mode = find_mode(driver, conn, mode_arg);
        auto const decoder = find_media(media_arg);
        auto const overlay = find_media(overlay_arg);

        if (seek_arg) {
            fmt::print("Seeking to {:.3f} seconds...\n", seek_arg);
            int64_t const millis = seek_arg * 1e3;
            decoder->seek_before(std::chrono::milliseconds(millis));
        }

        play_video(decoder, overlay, tiff_arg, driver, conn, mode);

        if (sleep_arg > 0) {
            fmt::print("Sleeping {:.1f} seconds...\n", sleep_arg);
            std::chrono::duration<double> sleep_time{sleep_arg};
            std::this_thread::sleep_for(sleep_time);
        }

        fmt::print("Done!\n\n");
    } catch (std::exception const& e) {
        fmt::print("*** {}\n", e.what());
    }

    return 0;
}

}  // namespace pivid
