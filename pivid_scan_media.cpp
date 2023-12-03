// Simple command line tool to print media and optionally save frames.

#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include "logging_policy.h"
#include "media_decoder.h"

namespace pivid {

// Main program, parses flags and opens a decoder.
extern "C" int main(int const argc, char const* const* const argv) {
    std::string log_arg;
    std::vector<std::string> media_arg;
    std::string frames_dir_arg;
    bool list_frames_arg = false;
    double seek_arg = 0.0;
    double stop_arg = 0.0;
    std::string prefix_arg;

    CLI::App app("Get information from a media file");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--media", media_arg, "Media file to play")->required();
    app.add_option("--frames_dir", frames_dir_arg, "Directory to save frames");
    app.add_option("--seek", seek_arg, "Seek this many seconds into media");
    app.add_option("--stop", stop_arg, "Stop this many seconds into media");
    app.add_flag("--list_frames", list_frames_arg, "Print frame metadata");
    CLI11_PARSE(app, argc, argv);

    configure_logging(log_arg);
    auto const logger = make_logger("pivid_scan_media");

    int errors = 0;
    for (auto const& filename : media_arg) {
        try {
            TRACE(logger, "Opening media: {}", filename);
            auto const decoder = open_media_decoder(filename);
            fmt::print("{}\n", debug(decoder->file_info()));

            if (seek_arg) {
                fmt::print("  Seeking to {:.3f}s...\n", seek_arg);
                decoder->seek_before(seek_arg);
            }

            if (list_frames_arg || !frames_dir_arg.empty()) {
                TRACE(logger, "Getting first frame...");
                for (;;) {
                    auto media_frame = decoder->next_frame();
                    if (!media_frame) {
                        fmt::print("  EOF\n");
                        break;
                    }

                    media_frame->image.source_comment.clear();  // Redundant
                    fmt::print("  {}\n", debug(*media_frame));

                    if (!frames_dir_arg.empty()) {
                        DEBUG(logger, "Encoding TIFF...");
                        auto tiff = debug_tiff(media_frame->image);

                        auto path = fmt::format(
                            "{}/{}.{:08.3f}s.tiff",
                            frames_dir_arg,
                            std::filesystem::path{filename}.stem().native(),
                            media_frame->time.begin
                        );

                        fmt::print("    {}\n", path);
                        std::ofstream ofs;
                        ofs.exceptions(~std::ofstream::goodbit);
                        ofs.open(path, std::ios::binary);
                        ofs.write((char const*) tiff.data(), tiff.size());
                    }

                    if (stop_arg && media_frame->time.end >= stop_arg) {
                        fmt::print("  Stop ({:.3f}s)\n", stop_arg);
                        break;
                    }
                    TRACE(logger, "Getting next frame...");
                }
            }
        } catch (std::exception const& e) {
            logger->critical("{}", e.what());
            ++errors;
        }

        if (list_frames_arg || !frames_dir_arg.empty()) fmt::print("\n");
    }

    return errors;
}

}  // namespace pivid
