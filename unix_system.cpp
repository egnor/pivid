#include "unix_system.h"

#include <unistd.h>

#include <system_error>

namespace pivid {

int check_errno(std::function<int()> f, std::string_view t, int ok_errno) {
    for (;;) {
        int const ret = f();
        if (ret >= 0 || errno == ok_errno) return ret;
        if (errno == EINTR) continue;  // Retry on signal interrupt.
        throw std::system_error(errno, std::system_category(), std::string{t});
    }
}

std::shared_ptr<int const> open_shared_fd(
    std::filesystem::path const& path, int flags, mode_t mode
) {
    auto const opener = [&] {return ::open(path.c_str(), flags, mode);};
    auto const deleter = [] (int* const fd) {::close(*fd); delete fd;};
    return {new int(check_errno(opener, path.native())), deleter};
}

}  // namespace pivid
