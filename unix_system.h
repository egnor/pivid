// Interfaces used for basic Unix system I/O.
// May be replaced by mocks for testing.

#pragma once

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include "interval_set.h"

namespace pivid {

// Preferred duration and time point representations.
using Seconds = std::chrono::duration<double>;
using SystemTime = std::chrono::sys_time<Seconds>;
using SteadyTime = std::chrono::time_point<std::chrono::steady_clock, Seconds>;

constexpr auto forever = Seconds{std::numeric_limits<double>::infinity()};

// The return from a system call, with an errno *or* some return value.
template <typename T>
struct [[nodiscard]] ErrnoOr {
    int err = 0;
    T value = {};

    // Throws for nonzero errno (with text), or returns value.
    T ex(std::string_view what) const& { check(what); return value; }
    T ex(std::string_view what) && { check(what); return std::move(value); }
    void check(std::string_view what) const {
        static auto const& cat = std::system_category();
        if (err) throw std::system_error(err, cat, std::string{what});
    }
};

// Interface to the operations on a Unix file descriptor.
// The functions manage retry on EINTR, and return ErrnoOr values.
// Returned by UnixSystem::open() and UnixSystem::adopt().
// *Internally synchronized* (by the OS, mainly) for multithreaded access.
class FileDescriptor {
  public:
    virtual ~FileDescriptor() = default;
    virtual int raw_fd() const = 0;
    virtual ErrnoOr<int> read(void* buf, size_t len) = 0;
    virtual ErrnoOr<int> write(void const* buf, size_t len) = 0;
    virtual ErrnoOr<int> ioctl(uint32_t nr, void* data) = 0;
    virtual ErrnoOr<std::shared_ptr<void>> mmap(size_t, int, int, off_t) = 0;

    // Executes a no-parameter ioctl, checking ioctl type.
    template <uint32_t nr>
    ErrnoOr<int> ioc() {
        static_assert(_IOC_DIR(nr) == _IOC_NONE && _IOC_SIZE(nr) == 0);
        return this->ioctl(nr, nullptr);
    }

    // Executes a user-to-kernel ioctl, checking object size & ioctl type.
    template <uint32_t nr, typename T>
    ErrnoOr<int> ioc(T const& v) {
        static_assert(std::is_standard_layout<T>::value);
        static_assert(_IOC_DIR(nr) == _IOC_WRITE && _IOC_SIZE(nr) == sizeof(T));
        return this->ioctl(nr, (void*) &v);
    }

    // Executes a user-to-kernel-and-back ioctl, checking size & ioctl type.
    template <uint32_t nr, typename T>
    ErrnoOr<int> ioc(T* v) {
        static_assert(std::is_standard_layout<T>::value);
        static_assert(_IOC_DIR(nr) == (_IOC_READ | _IOC_WRITE));
        static_assert(_IOC_SIZE(nr) == sizeof(T));
        return this->ioctl(nr, v);
    }
};

// Interface to the Unix OS. Normally a singleton returned by global_system().
// *Internally synchronized* (by the OS, mainly) for multithreaded access.
class UnixSystem {
  public:
    virtual ~UnixSystem() = default;

    // System clocks
    virtual SystemTime system_time() const = 0;
    virtual SteadyTime steady_time() const = 0;
    virtual void sleep_until(SteadyTime) const = 0;

    // Filesystem operations
    virtual ErrnoOr<struct stat> stat(std::string const&) const = 0;
    virtual ErrnoOr<std::string> realpath(std::string const&) const = 0;
    virtual ErrnoOr<std::vector<std::string>> list(
        std::string const& dir
    ) const = 0;

    // Returns a file descriptor for a file, like open().
    virtual ErrnoOr<std::unique_ptr<FileDescriptor>> open(
        std::string const&, int flags, mode_t mode = 0
    ) = 0;

    // Adopts a raw file descriptor. Takes ownership of closing the file.
    virtual std::unique_ptr<FileDescriptor> adopt(int raw_fd) = 0;

    // Subprocess management, like posix_spawn() and waitid().
    virtual ErrnoOr<pid_t> spawn(
        std::string const& command,
        std::vector<std::string> const& argv,
        posix_spawn_file_actions_t const* = nullptr,
        posix_spawnattr_t const* = nullptr,
        std::optional<std::vector<std::string>> const& environ = {}
    ) = 0;
    virtual ErrnoOr<siginfo_t> wait(idtype_t, id_t, int flags) = 0;
};

// Returns the singleton Unix access interface.
std::shared_ptr<UnixSystem> global_system();

// Date parser.
SystemTime parse_system_time(std::string const&);
std::string format_system_time(SystemTime);

// Debugging descriptions of values.
std::string debug(Seconds);
std::string debug(Interval<Seconds>);
std::string debug(IntervalSet<Seconds> const&);
std::string debug(SteadyTime);
std::string debug(SystemTime t);

}  // namespace pivid
