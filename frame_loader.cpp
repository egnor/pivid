#include "frame_loader.h"

#include <iterator>
#include <mutex>
#include <set>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "logging_policy.h"
#include "thread_signal.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& loader_logger() {
    static const auto logger = make_logger("loader");
    return logger;
}

class ThreadFrameLoader : public FrameLoader {
  public:
    virtual ~ThreadFrameLoader() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            logger->debug("Stopping loader thread ({})...", filename);
            shutdown = true;
            lock.unlock();
            wakeup->set();
            thread.join();
        }
    }

    virtual void set_request(
        RangeSet<Seconds> const& wanted,
        std::shared_ptr<ThreadSignal> notify
    ) {
        std::unique_lock lock{mutex};
        this->notify = notify;
        if (wanted != this->wanted) {
            auto to_erase = res.done;
            to_erase.erase(wanted);
            for (auto erase_range : to_erase) {
                res.done.erase(erase_range);
                res.frames.erase(
                    res.frames.lower_bound(erase_range.begin),
                    res.frames.lower_bound(erase_range.end)
                );
            }

            this->wanted = wanted;
            lock.unlock();
            wakeup->set();
        }
    }

    virtual Results results() const {
        std::scoped_lock lock{mutex};
        return res;
    }

    void start(
        DisplayDriver* display,
        std::string const& filename,
        std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
    ) {
        this->display = display;
        this->filename = filename;
        this->opener = opener;
        thread = std::thread(&ThreadFrameLoader::loader_thread, this);
    }

    void loader_thread() {
        std::unique_lock lock{mutex};
        logger->debug("Loader thread ({}) running...", filename);

        std::map<Seconds, Reader> readers;
        std::map<Seconds, Reader> spare_readers;
        while (!shutdown) {
            bool progress = false;

            // TODO what about when a window is temporarily filled?
            // Different treatment for a reader adjacent to wanted?

            for (auto iter = readers.begin(); iter != readers.end(); ++iter) {
            }

            for (auto const& [time, reader] : readers) {
                if (!wanted.contains(time) || res.done.contains(time)) {
                    spare_readers.insert(readers.extract(time));
                }
            }

            auto to_load = wanted;
            to_load.erase(res.done);
            for (auto load_range : to_load) {
                auto read_iter = readers.find(load_range.begin);
                (void) read_iter;
            }

            if (progress) {
                if (notify) notify->set();
            } else if (!progress) {
                lock.unlock();
                wakeup->wait();
                lock.lock();
            }
        }

        logger->debug("Loader thread ({}) ending...", filename);
    }

  private:
    struct Reader {
        std::unique_ptr<MediaDecoder> decoder;
        Seconds last_seek = {};
        Seconds next_fetch = {};
    };

    std::shared_ptr<log::logger> logger = loader_logger();

    DisplayDriver* display;
    std::string filename;
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener;

    std::mutex mutable mutex;
    std::thread thread;
    std::shared_ptr<ThreadSignal> wakeup = make_signal();

    RangeSet<Seconds> wanted;
    std::shared_ptr<ThreadSignal> notify;
    Results res;

    bool shutdown = false;
};

}  // anonymous namespace

std::unique_ptr<FrameLoader> make_frame_loader(
    DisplayDriver* display,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
) {
    auto loader = std::make_unique<ThreadFrameLoader>();
    loader->start(display, filename, std::move(opener));
    return loader;
}

std::string debug(Range<Seconds> const& range) {
    return fmt::format("{:.3}~{:.3}", range.begin, range.end);
}

std::string debug(RangeSet<Seconds> const& set) {
    std::string out = "{";
    for (auto const& range : set) {
        if (out.size() > 1) out += ", ";
        out += debug(range);
    }
    return out + "}";
}

}  // namespace pivid
