// Simple command line tool to list DRM/KMS resources and their IDs.

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include <fmt/core.h>
#include <gflags/gflags.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>

DEFINE_bool(verbose, false, "Print detailed properties");

void scan_media(const std::string &dir) {
    AVIODirContext *dir = nullptr;
    if (avio_open_dir(&dir, ".", nullptr) < 0) {
        fmt::print("*** Error reading dir: {}", dir);
        exit(1);
    }

    AVIODirEntry *entry = nullptr;
    while (avio_read_dir(dir, &entry) >= 0) {
        avio_free_directory_entry(&entry);
    }

    avio_close_dir(&dir);
}

void inspect_media(const std::string& media) {
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
