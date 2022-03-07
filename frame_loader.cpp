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

    virtual void set_wanted(
        std::vector<Request> const& wanted,
        std::shared_ptr<ThreadSignal> notify
    ) {
        std::unique_lock lock{mutex};
        this->wanted = wanted;
        this->notify = notify;
        wakeup->set();
        
    }

    virtual Frames loaded() const {
        std::unique_lock lock{mutex};
        return frames;
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
        while (!shutdown) {
            bool progress = false;

            std::vector<Reader> spare_readers;
            Seconds last_end = {};

            if (!progress) {
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
        Seconds last_fetch = {};
    };

    std::shared_ptr<log::logger> logger = loader_logger();

    DisplayDriver* display;
    std::string filename;
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener;

    std::mutex mutable mutex;
    std::thread thread;
    std::shared_ptr<ThreadSignal> wakeup = make_signal();

    std::vector<Request> wanted;
    std::shared_ptr<ThreadSignal> notify = {};
    Frames frames;

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

std::string debug(FrameLoader::Request const& req) {
    return fmt::format("{:.3}~{:.3}", req.begin, req.end);
}

}  // namespace pivid
