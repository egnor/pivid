// Simple command line tool to list media files and their contents.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>

extern "C" {
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
}

DEFINE_bool(verbose, false, "Print detailed properties");

void scan_media(const std::string &dir_path) {
    AVIODirContext *dir = nullptr;
    AVDictionary *options = nullptr;
    if (avio_open_dir(&dir, dir_path.c_str(), &options) < 0) {
        fmt::print("*** Error reading dir: {}\n", dir_path);
        exit(1);
    }

    AVIODirEntry *entry = nullptr;
    std::vector<std::string> possible;
    while (avio_read_dir(dir, &entry) >= 0 && entry != nullptr) {
        if (entry->type != AVIO_ENTRY_DIRECTORY) {
            AVProbeData probe = {};
            probe.filename = entry->name;
            const auto* const format = av_probe_input_format(&probe, false);
            if (format) {
                possible.push_back(fmt::format("{}/{}", dir_path, entry->name));
            }
        }
        avio_free_directory_entry(&entry);
    }

    std::sort(possible.begin(), possible.end());
    for (const auto &file_path : possible) {
        fmt::print("{}\n", file_path);
    }

    avio_close_dir(&dir);
}

void inspect_media(const std::string& media) {
    (void) media;
}

DEFINE_string(dir, ".", "Directory to scan for media");
DEFINE_string(media, "", "Media file or URL to inspect");

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    if (!FLAGS_media.empty()) {
        inspect_media(FLAGS_media);
    } else {
        scan_media(FLAGS_dir);
    }
    return 0;
}
