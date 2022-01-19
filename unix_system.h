#pragma once

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

namespace pivid {

template <typename T>
struct [[nodiscard]] ErrnoOr {
    int err = 0;
    T value = {};
    T ex(std::string_view w) const {
        static auto const& cat = std::system_category();
        if (err) throw std::system_error(err, cat, std::string{w});
        return value;
    }
};

class FileDescriptor {
  public:
    virtual ~FileDescriptor() = default;
    virtual int raw_fd() const = 0;
    virtual ErrnoOr<int> read(void* buf, size_t len) = 0;
    virtual ErrnoOr<int> ioctl(uint32_t nr, void* data) = 0;
    virtual ErrnoOr<std::shared_ptr<void>> mmap(size_t, int, int, off_t) = 0;

    template <uint32_t nr>
    ErrnoOr<int> ioc() {
        static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
        return this->ioctl(nr, nullptr);
    }

    template <uint32_t nr, typename T>
    ErrnoOr<int> ioc(T const& v) {
        static_assert(std::is_standard_layout<T>::value);
        static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
        return this->ioctl(nr, (void*) &v);
    }

    template <uint32_t nr, typename T>
    ErrnoOr<int> ioc(T* v) {
        static_assert(std::is_standard_layout<T>::value);
        static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
        static_assert(_IOC_SIZE(nr) == sizeof(T));
        return this->ioctl(nr, v);
    }
};

class UnixSystem {
  public:
    virtual ~UnixSystem() = default;

    virtual ErrnoOr<std::shared_ptr<FileDescriptor>> open(
        std::string const&, int flags, mode_t mode = 0
    ) = 0;

    virtual ErrnoOr<std::vector<std::string>> list(std::string const& dir) = 0;
    virtual ErrnoOr<struct stat> stat(std::string const&) = 0;
    virtual ErrnoOr<std::string> realpath(std::string const&) = 0;
};

UnixSystem* global_system();

}  // namespace pivid
