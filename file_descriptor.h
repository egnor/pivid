#pragma once

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <system_error>

namespace pivid {

// If ret is negative, throws system_error based on errno & the supplied text.
inline int check_sys(
    int ret, std::string_view note, std::string_view detail = ""
) {
    if (ret >= 0) return ret;  // No error.
    auto what = std::string(note);
    if (!detail.empty()) ((what += " (") += detail) += ")";
    throw std::system_error(errno, std::system_category(), what);
}

// Manages a Unix file descriptor, taking responsibility to close on delete.
class FileDescriptor {
  public:
    FileDescriptor() {}
    ~FileDescriptor() noexcept { if (f >= 0) ::close(f); }
    FileDescriptor(FileDescriptor const&) = delete;
    FileDescriptor& operator=(FileDescriptor const&) = delete;

    int const& fd() const { return f; }

    void init(int fd, std::string_view note, std::string_view detail = "") {
        assert(f < 0);
        f = check_sys(fd, note, detail);
    }

    template <uint32_t nr>
    int io(std::string_view note, std::string_view detail = "") {
        static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
        return check_sys(retry_ioctl(nr, nullptr), note, detail);
    }

    template <uint32_t nr, typename T>
    int io(T const& val, std::string_view note, std::string_view detail = "") {
        static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
        return check_sys(retry_ioctl(nr, (void*) &val), note, detail);
    }

    template <uint32_t nr, typename T>
    int io(T* val, std::string_view note, std::string_view detail = "") {
        static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
        static_assert(_IOC_SIZE(nr) == sizeof(T));
        return check_sys(retry_ioctl(nr, val), note, detail);
    }

  private:
    int f = -1;

    int retry_ioctl(uint32_t io, void* arg) {
        int ret;
        do { ret = ::ioctl(f, io, arg); } while (ret < 0 && errno == EINTR);
        return ret;
    }
};

}  // namespace pivid
