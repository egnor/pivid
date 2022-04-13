#include "unix_system.h"

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>

#include <fmt/core.h>

#include "logging_policy.h"

namespace pivid {

namespace {

inline ErrnoOr<int> run_sys(std::function<int()> f) {
    ErrnoOr<int> ret;
    ret.value = f();
    ret.err = errno;
    if (ret.value < 0 && ret.err == EINTR) return run_sys(f);
    return (ret.value >= 0) ? ErrnoOr<int>{0, ret.value} : ret;
}

class FileDescriptorDef : public FileDescriptor {
  public:
    FileDescriptorDef(int fd) : fd(fd) {}
    virtual ~FileDescriptorDef() final { ::close(fd); }
    virtual int raw_fd() const final { return fd; }

    virtual ErrnoOr<int> read(void* buf, size_t len) final {
        return run_sys([&] {return ::read(fd, buf, len);});
    }

    virtual ErrnoOr<int> write(void const* buf, size_t len) final {
        return run_sys([&] {return ::write(fd, buf, len);});
    }

    virtual ErrnoOr<int> ioctl(uint32_t nr, void* buf) final {
        return run_sys([&] {return ::ioctl(fd, nr, buf);});
    }

    virtual ErrnoOr<std::shared_ptr<void>> mmap(
        size_t len, int prot, int flags, off_t off
    ) final {
        void* const mem = ::mmap(nullptr, len, prot, flags, fd, off);
        if (mem == MAP_FAILED) return {errno, {}};
        return {0, {mem, [len](void* m) {::munmap(m, len);}}};
    }

  private:
    int fd = -1;
};

class SyncFlagDef : public SyncFlag {
  public:
    SyncFlagDef(clockid_t clockid) {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, clockid);
        pthread_cond_init(&cond, &attr);
        pthread_condattr_destroy(&attr);
    }

    virtual ~SyncFlagDef() final { pthread_cond_destroy(&cond); }

    virtual void set() final {
        pthread_mutex_lock(&mutex);
        if (!wake_flag) {
            wake_flag = true;
            pthread_cond_signal(&cond);
        }
        pthread_mutex_unlock(&mutex);
    }

    virtual void sleep() final {
        pthread_mutex_lock(&mutex);
        while (!wake_flag)
            pthread_cond_wait(&cond, &mutex);
        wake_flag = false;
        pthread_mutex_unlock(&mutex);
    }

    virtual bool sleep_until(double t) final {
        struct timespec ts;
        ts.tv_sec = t;
        ts.tv_nsec = (t - ts.tv_sec) * 1e9;
        pthread_mutex_lock(&mutex);
        while (!wake_flag) {
            if (pthread_cond_timedwait(&cond, &mutex, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&mutex);
                return false;
            }
        }
        wake_flag = false;
        pthread_mutex_unlock(&mutex);
        return true;
    }

  private:
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond;
    bool wake_flag = false;
};

class UnixSystemDef : public UnixSystem {
  public:
    virtual double clock(clockid_t clockid) const final {
        struct timespec ts;
        clock_gettime(clockid, &ts);
        return ts.tv_sec + 1e-9 * ts.tv_nsec;
    }

    virtual std::unique_ptr<SyncFlag> make_flag(clockid_t clockid) const final {
        return std::make_unique<SyncFlagDef>(clockid);
    }

    virtual ErrnoOr<std::vector<std::string>> ls(
        std::string const& dir
    ) const final {
        std::unique_ptr<DIR, int (*)(DIR*)> dp(opendir(dir.c_str()), closedir);
        if (!dp) return {errno, {}};

        ErrnoOr<std::vector<std::string>> ret;
        while (dirent* ent = readdir(dp.get()))
            ret.value.push_back(ent->d_name);
        std::sort(ret.value.begin(), ret.value.end());
        return ret;
    }

    virtual ErrnoOr<struct stat> stat(std::string const& path) const final {
        ErrnoOr<struct stat> ret;
        ret.err = run_sys([&] {return ::stat(path.c_str(), &ret.value);}).err;
        return ret;
    }

    virtual ErrnoOr<std::string> realpath(std::string const& path) const final {
        char buf[PATH_MAX];
        if (!::realpath(path.c_str(), buf)) return {errno, {}};
        return {0, buf};
    }

    virtual ErrnoOr<std::unique_ptr<FileDescriptor>> open(
        std::string const& path, int flags, mode_t mode
    ) final {
        auto const r = run_sys([&] {return ::open(path.c_str(), flags, mode);});
        if (r.value < 0) return {r.err ? r.err : EBADF, {}};
        return {0, adopt(r.value)};
    }

    virtual std::unique_ptr<FileDescriptor> adopt(int raw_fd) final {
        return std::make_unique<FileDescriptorDef>(raw_fd);
    }

    virtual ErrnoOr<pid_t> spawn(
        std::string const& command,
        std::vector<std::string> const& argv,
        posix_spawn_file_actions_t const* actions,
        posix_spawnattr_t const* attr,
        std::optional<std::vector<std::string>> const& envp
    ) final {
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

    virtual ErrnoOr<siginfo_t> wait(idtype_t idtype, id_t id, int flags) final {
        siginfo_t s = {};
        auto const r = run_sys([&] { return ::waitid(idtype, id, &s, flags); });
        return {r.err, s};
    }
};

}  // namespace

std::shared_ptr<UnixSystem> global_system() {
    static const auto system = std::make_shared<UnixSystemDef>();
    return system;
}

double parse_realtime(std::string const& s) {
    size_t end;
    double const d = std::stod(s, &end);
    if (!s.empty() && end >= s.size())
        return d;

    int year = 0, mon = 0, day = 0, hr = 0, min = 0, sec = 0, tzh = 0, tzm = 0;
    char sep = '\0', tzsep = '\0';
    char frac[21] = "";
    int const scanned = sscanf(
        s.c_str(), "%d-%d-%d%c %d:%d:%d%20[,.0-9]%c%d:%d", 
        &year, &mon, &day, &sep, &hr, &min, &sec, frac, &tzsep, &tzh, &tzm
    );

    CHECK_ARG(scanned >= 7, "Bad date format: \"{}\"", s);
    CHECK_ARG(sep == 'T' || sep == ' ', "Bad date separator: \"{}\"", s);
    struct tm parts = {};
    parts.tm_sec = sec;
    parts.tm_min = min;
    parts.tm_hour = hr;
    parts.tm_mday = day;
    parts.tm_mon = mon - 1;
    parts.tm_year = year - 1900;

    time_t const tt = timegm(&parts);
    CHECK_ARG(tt != (time_t) -1, "Date overflow: \"{}\"", s);

    double extra = 0.0;
    if (scanned >= 8 && frac[0] != '\0') {
        CHECK_ARG(frac[0] == '.' || frac[0] == ',', "Bad fraction: \"{}\"", s);
        frac[0] = '.';
        extra = atof(frac);
    }

    int offset = 0;
    if (scanned >= 9) {
        if (tzsep == 'Z' || tzsep == 'z') {
            CHECK_ARG(scanned == 9, "Bad UTC date: \"{}\"", s);
        } else {
            CHECK_ARG(tzsep == '+' || tzsep == '-', "Bad TZ format: \"{}\"", s);
            CHECK_ARG(scanned == 11, "Bad TZ offset: \"{}\"", s);
            offset = (tzsep == '-' ? -1 : 1) * (tzh * 3600 + tzm * 60);
        }
    }

    return tt + extra - offset;
}

std::string format_realtime(double t) {
    struct tm parts = {};
    time_t tt = t;
    gmtime_r(&tt, &parts);
    return fmt::format(
        "{}-{:02d}-{:02d} {:02d}:{:02d}:{:06.3f}Z",
        parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
        parts.tm_hour, parts.tm_min, parts.tm_sec + (t - tt)
    );
}

std::string abbrev_realtime(double t) {
    struct tm parts = {};
    time_t tt = t;
    gmtime_r(&tt, &parts);
    return fmt::format(
        "{:02d}:{:02d}:{:06.3f}",
        parts.tm_hour, parts.tm_min, parts.tm_sec + (t - tt)
    );
}

}  // namespace pivid
