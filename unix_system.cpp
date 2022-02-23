#include "unix_system.h"

#include <dirent.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace pivid {

namespace {

inline ErrnoOr<int> run_sys(std::function<int()> f) {
    ErrnoOr<int> ret;
    ret.value = f();
    ret.err = errno;
    if (ret.value < 0 && ret.err == EINTR) return run_sys(f);
    return (ret.value >= 0) ? ErrnoOr<int>{0, ret.value} : ret;
}

class SystemFileDescriptor : public FileDescriptor {
  public:
    SystemFileDescriptor(int fd) : fd(fd) {}
    virtual ~SystemFileDescriptor() { ::close(fd); }
    virtual int raw_fd() const { return fd; }

    virtual ErrnoOr<int> read(void* buf, size_t len) {
        return run_sys([&] {return ::read(fd, buf, len);});
    }

    virtual ErrnoOr<int> write(void const* buf, size_t len) {
        return run_sys([&] {return ::write(fd, buf, len);});
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
    virtual SystemTime system_time() const {
        using std::chrono::time_point_cast;
        return time_point_cast<Seconds>(std::chrono::system_clock::now());
    }

    virtual SteadyTime steady_time() const {
        using std::chrono::time_point_cast;
        return time_point_cast<Seconds>(std::chrono::steady_clock::now());
    }

    virtual void sleep_until(SteadyTime t) const {
        using std::chrono::time_point_cast;
        auto u = time_point_cast<std::chrono::steady_clock::duration>(t);
        std::this_thread::sleep_until(u);
    }

    virtual ErrnoOr<std::vector<std::string>> list(
        std::string const& dir
    ) const {
        std::unique_ptr<DIR, int (*)(DIR*)> dp(opendir(dir.c_str()), closedir);
        if (!dp) return {errno, {}};

        ErrnoOr<std::vector<std::string>> ret;
        while (dirent* ent = readdir(dp.get()))
            ret.value.push_back(ent->d_name);
        return ret;
    }

    virtual ErrnoOr<struct stat> stat(std::string const& path) const {
        ErrnoOr<struct stat> ret;
        ret.err = run_sys([&] {return ::stat(path.c_str(), &ret.value);}).err;
        return ret;
    }

    virtual ErrnoOr<std::string> realpath(std::string const& path) const {
        char buf[PATH_MAX];
        if (!::realpath(path.c_str(), buf)) return {errno, {}};
        return {0, buf};
    }

    virtual ErrnoOr<std::shared_ptr<FileDescriptor>> open(
        std::string const& path, int flags, mode_t mode
    ) {
        auto const r = run_sys([&] {return ::open(path.c_str(), flags, mode);});
        if (r.value < 0) return {r.err ? r.err : EBADF, {}};
        return {0, adopt(r.value)};
    }

    virtual std::shared_ptr<FileDescriptor> adopt(int raw_fd) {
        return std::make_shared<SystemFileDescriptor>(raw_fd);
    }

    virtual ErrnoOr<pid_t> spawn(
        std::string const& command,
        std::vector<std::string> const& argv,
        posix_spawn_file_actions_t const* actions,
        posix_spawnattr_t const* attr,
        std::optional<std::vector<std::string>> const& envp
    ) {
        auto const c_vector = [](std::vector<std::string> const& vec) {
            std::vector<char*> ret;
            for (auto& s : vec) ret.push_back(const_cast<char*>(s.c_str()));
            return ret;
        };

        pid_t pid = 0;
        auto const r = run_sys([&] {
            return ::posix_spawnp(
                &pid, command.c_str(), actions, attr,
                c_vector(argv).data(),
                envp ? c_vector(*envp).data() : environ
            );
        });
        return {r.err, pid};
    }

    virtual ErrnoOr<siginfo_t> wait(idtype_t idtype, id_t id, int flags) {
        siginfo_t s = {};
        auto const r = run_sys([&] { return ::waitid(idtype, id, &s, flags); });
        return {r.err, s};
    }
};

}  // namespace

std::shared_ptr<UnixSystem> global_system() {
    static const auto system = std::make_shared<GlobalSystem>();
    return system;
}

}  // namespace pivid
