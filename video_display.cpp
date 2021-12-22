#include "video_display.h"

#include <errno.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#undef NDEBUG
#include <assert.h>

#include <cctype>
#include <filesystem>

#include <fmt/core.h>

namespace pivid {

namespace {

//
// DRM/KMS-specific error handling
//

class DrmError : public DisplayError {
  public:
    using str = std::string_view;
    DrmError(str action, str note, str error, int errcode = 0) {
        text = std::string{action};
        if (!note.empty()) text += fmt::format(" ({})", note);
        if (!error.empty()) text += fmt::format(": {}", error);
        if (errcode) text += ": " + std::system_category().message(errcode);
    }

    virtual char const* what() const noexcept { return text.c_str(); }

  private:
    std::string text;
};

int check_ret(std::string_view action, std::string_view note, int ret) {
    if (ret >= 0) return ret;  // No error.
    throw DrmError(action, note, "", errno);
}

}  // anonymous namespace

std::vector<DisplayDeviceName> list_devices() {
    std::vector<DisplayDeviceName> out;

    try {
        std::filesystem::path const dri_dir = "/dev/dri";
        for (auto const& entry : std::filesystem::directory_iterator(dri_dir)) {
            std::string const name = entry.path().filename();
            if (name.substr(0, 4) != "card" || !isdigit(name[4])) continue;

            struct stat fstat;
            check_ret("Stat", name, stat(entry.path().c_str(), &fstat));
            if ((fstat.st_mode & S_IFMT) != S_IFCHR)
                throw DrmError("Device", name, "Not a char special device");

            auto const maj = major(fstat.st_rdev), min = minor(fstat.st_rdev);
            auto const sys_dev = fmt::format("/sys/dev/char/{}:{}", maj, min);
            auto const sys_devices = std::filesystem::canonical(sys_dev);
            out.push_back({
                entry.path(),
                std::filesystem::relative(sys_devices, "/sys/devices").native()
            });
        }
    } catch (std::filesystem::filesystem_error const& e) {
        throw DrmError("File", e.path1().native(), "", e.code().value());
    }

    return out;
}

}  // namespace pivid
