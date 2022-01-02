#pragma once

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <filesystem>
#include <functional>
#include <string>
#include <type_traits>

namespace pivid {

int check_errno(std::function<int()>, std::string_view what, int ok_errno = 0);

std::shared_ptr<int const> open_shared_fd(
    std::filesystem::path const& path, int flags, mode_t mode = 0
);

template <uint32_t nr>
int ioc(int fd, std::string_view what, int ok_errno = 0) {
    static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
    return check_errno([&] {return ioctl(fd, nr, nullptr);}, what, ok_errno);
}

template <uint32_t nr, typename T>
int ioc(int fd, T const& v, std::string_view what, int ok_errno = 0) {
    static_assert(std::is_standard_layout<T>::value);
    static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
    return check_errno([&] {return ioctl(fd, nr, (void*) &v);}, what, ok_errno);
}

template <uint32_t nr, typename T>
int ioc(int fd, T* v, std::string_view what, int ok_errno = 0) {
    static_assert(std::is_standard_layout<T>::value);
    static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
    static_assert(_IOC_SIZE(nr) == sizeof(T));
    return check_errno([&] {return ioctl(fd, nr, v);}, what, ok_errno);
}

}  // namespace pivid
