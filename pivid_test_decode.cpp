// Simple command line tool to exercise video decoding and playback.

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>

#include <thread>

#include "media_decoder.h"

// Main program, parses flags and calls the decoder loop.
int main(int const argc, char const* const* const argv) {
    std::string media_file;

    CLI::App app("Decode and show a media file");
    app.add_option("--media", media_file, "Media file or URL")->required();
    CLI11_PARSE(app, argc, argv);

    try {
        auto const decoder = pivid::new_media_decoder(media_file);
        while (!decoder->at_eof()) {
            auto const frame = decoder->next_frame();
            if (frame) {
                fmt::print("FRAME\n");
            } else {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(0.01s);
            }
        }
    } catch (pivid::MediaError const& e) {
        fmt::print("*** {}\n", e.what());
    }

    return 0;
}
