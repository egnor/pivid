#include "thread_signal.h"

#include <mutex>
#include <condition_variable>

namespace pivid {

// TODO: Replace with std::binary_semaphore once available.
class CondVarSignal : public ThreadSignal {
  public:
    virtual void set() {
        std::lock_guard<std::mutex> lock{mutex};
        if (!signal_count) {
            ++signal_count;
            condvar.notify_one();
        }
    }

    virtual void wait() {
        std::unique_lock<std::mutex> lock{mutex};
        while (!signal_count)
            condvar.wait(lock);
        --signal_count;
    }

    virtual bool wait_until(SteadyTime t) {
        using std::chrono::time_point_cast;
        auto u = time_point_cast<std::chrono::steady_clock::duration>(t);
        std::unique_lock<std::mutex> lock{mutex};
        while (!signal_count) {
            if (condvar.wait_until(lock, u) == std::cv_status::timeout)
                return false;
        }
        --signal_count;
        return true;
    }

  private:
    std::mutex mutex;
    std::condition_variable condvar;
    int signal_count = 0;
};

std::shared_ptr<ThreadSignal> make_signal() {
    return std::make_shared<CondVarSignal>();
}

}  // namespace pivid
