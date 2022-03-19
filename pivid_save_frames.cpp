// Simple command line tool to exercise video decoding and playback.

#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/chrono.h>
#include <fmt/core.h>

extern "C" {
#include <libavutil/log.h>
}

#include "logging_policy.h"
#include "media_decoder.h"

namespace pivid {

// Main program, parses flags and calls the decoder loop.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string log_arg;
    std::string media_arg;
    double seek_arg = 0.0;
    std::string prefix_arg = "frame";

    CLI::App app("Decode and show a media file");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--media", media_arg, "Media file to play")->required();
    app.add_option("--seek", seek_arg, "Seek this many seconds into media");
    app.add_option("--frame_prefix", prefix_arg, "Prefix for .tiff filenames");
    CLI11_PARSE(app, argc, argv);

    try {
        configure_logging(log_arg);
        auto const logger = make_logger("main");

        logger->info("Opening media: {}", media_arg);
        auto const decoder = open_media_decoder(media_arg);

        if (seek_arg) {
            fmt::print("Seeking to {:.3f} seconds...\n", seek_arg);
            decoder->seek_before(Seconds(seek_arg));
        }

        int frame_index = 0;
        logger->trace("Getting first video frame...");
        while (decoder) {
            auto const media_frame = decoder->next_frame();
            if (!media_frame) break;

            logger->trace("Encoding TIFF...");
            auto path = fmt::format("{}.F{:05d}.tiff", prefix_arg, frame_index);
            auto tiff = debug_tiff(media_frame->image);

            logger->info("Saving: {}", path);
            std::ofstream ofs;
            ofs.exceptions(~std::ofstream::goodbit);
            ofs.open(path, std::ios::binary);
            ofs.write((char const*) tiff.data(), tiff.size());

            ++frame_index;
            logger->trace("Getting next video frame (#{})...", frame_index);
        }

        logger->info("End of media file reached");
    } catch (std::exception const& e) {
        fmt::print("*** {}\n", e.what());
    }

    return 0;
}

}  // namespace pivid
