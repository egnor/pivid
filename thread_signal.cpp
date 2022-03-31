#include "thread_signal.h"

#include <mutex>
#include <condition_variable>

namespace pivid {

namespace {

// TODO: Replace with std::binary_semaphore once available.
class ThreadSignalDef : public ThreadSignal {
  public:
    virtual void set() final {
        std::scoped_lock<std::mutex> lock{mutex};
        if (!signal_flag) {
            signal_flag = true;
            condvar.notify_one();
        }
    }

    virtual bool test() const final {
        std::scoped_lock<std::mutex> lock{mutex};
        return signal_flag;
    }

    virtual void wait() final {
        std::unique_lock<std::mutex> lock{mutex};
        while (!signal_flag)
            condvar.wait(lock);
        signal_flag = false;
    }

    virtual bool wait_until(double t) final {
        using namespace std::chrono;
        duration<double> const double_d{t};
        auto const system_d = duration_cast<system_clock::duration>(double_d);
        system_clock::time_point system_t(system_d);
        std::unique_lock<std::mutex> lock{mutex};
        while (!signal_flag) {
            if (condvar.wait_until(lock, system_t) == std::cv_status::timeout)
                return false;
        }
        signal_flag = false;
        return true;
    }

  private:
    std::mutex mutable mutex;
    std::condition_variable condvar;
    bool signal_flag = false;
};

}  // anonymous namespace

std::unique_ptr<ThreadSignal> make_signal() {
    return std::make_unique<ThreadSignalDef>();
}

}  // namespace pivid
