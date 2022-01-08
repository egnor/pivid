#include "unix_system.h"

#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <functional>

namespace pivid {

namespace {

inline ErrnoOr<int> run_sys(std::function<int()> f) {
    ErrnoOr<int> ret;
    ret.value = f();
    ret.err = errno;
    if (ret.value < 0 && ret.err == EINTR) return run_sys(f);
    return (ret.value >= 0) ? ErrnoOr<int>{0, ret.value} : ret;
}

class GlobalFileDescriptor : public FileDescriptor {
  public:
    GlobalFileDescriptor(int fd) : fd(fd) {}
    virtual ~GlobalFileDescriptor() { ::close(fd); }
    virtual int raw_fd() const { return fd; }

    virtual ErrnoOr<int> read(void* buf, size_t len) {
        return run_sys([&] {return ::read(fd, buf, len);});
    }

    virtual ErrnoOr<int> ioctl(uint32_t nr, void* buf) {
        return run_sys([&] {return ::ioctl(fd, nr, buf);});
    }

    virtual ErrnoOr<std::shared_ptr<void>> mmap(
        size_t len, int prot, int flags, off_t off
    ) {
        void* const mem = ::mmap(nullptr, len, prot, flags, fd, off);
        if (mem == MAP_FAILED) return {errno, {}};
        return {0, {mem, [len](void* m) {::munmap(m, len);}}};
    }

  private:
    int fd = -1;
};

class GlobalSystem : public UnixSystem {
  public:
    virtual ErrnoOr<std::vector<std::string>> list(std::string const& dir) {
        std::unique_ptr<DIR, int (*)(DIR*)> dp(opendir(dir.c_str()), closedir);
        if (!dp) return {errno, {}};

        ErrnoOr<std::vector<std::string>> ret;
        while (dirent* ent = readdir(dp.get()))
            ret.value.push_back(ent->d_name);
        return ret;
    }

    virtual ErrnoOr<struct stat> stat(std::string const& path) {
        ErrnoOr<struct stat> ret;
        ret.err = run_sys([&] {return ::stat(path.c_str(), &ret.value);}).err;
        return ret;
    }

    virtual ErrnoOr<std::string> realpath(std::string const& path) {
        char buf[PATH_MAX];
        if (!::realpath(path.c_str(), buf)) return {errno, {}};
        return {0, buf};
    }

    virtual ErrnoOr<std::shared_ptr<FileDescriptor>> open(
        std::string const& path, int flags, mode_t mode
    ) {
        auto const r = run_sys([&] {return ::open(path.c_str(), flags, mode);});
        if (r.err) return {r.err, {}};
        return {0, std::make_shared<GlobalFileDescriptor>(r.value)};
    }
};

}  // namespace

UnixSystem* global_system() {
    static GlobalSystem system;
    return &system;
}

}  // namespace pivid
