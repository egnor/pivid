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

// Manages a Unix file descriptor, taking responsibility to close on delete.
class FileDescriptor {
  public:
    FileDescriptor(int const fd) : f(fd) {}
    ~FileDescriptor() noexcept { if (f >= 0) ::close(f); }
    FileDescriptor(FileDescriptor const&) = delete;
    FileDescriptor& operator=(FileDescriptor const&) = delete;
    int const& fd() const { return f; }

  private:
    int const f = -1;
};

// Invokes a Unix syscall style functor, loops on EINTR, throws on other errors.
template <typename F>
inline int check_sys(F&& f, std::string_view t, int ok_errno = 0) {
    for (;;) {
        int const ret = f();
        if (ret >= 0 || errno == ok_errno) return ret;
        if (errno == EINTR) continue;  // Retry on signal interrupt.
        throw std::system_error(errno, std::system_category(), std::string{t});
    }
}

template <uint32_t nr>
int ioc(FileDescriptor const& f, std::string_view t) {
    static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
    return check_sys([&] {return ::ioctl(f.fd(), nr, nullptr);}, t);
}

template <uint32_t nr, typename T>
int ioc(FileDescriptor const& f, T const& v, std::string_view t) {
    static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
    return check_sys([&] {return ::ioctl(f.fd(), nr, (void*) &v);}, t);
}

template <uint32_t nr, typename T>
int ioc(FileDescriptor const& f, T* v, std::string_view t) {
    static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
    static_assert(_IOC_SIZE(nr) == sizeof(T));
    return check_sys([&] {return ::ioctl(f.fd(), nr, v);}, t);
}

}  // namespace pivid
