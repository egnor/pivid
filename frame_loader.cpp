#include "frame_loader.h"

#include <condition_variable>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "logging_policy.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& loader_logger() {
    static const auto logger = make_logger("loader");
    return logger;
}

class ThreadFrameWindow;

struct SharedState {
    std::shared_ptr<log::logger> const logger = loader_logger();
    DisplayDriver* display = nullptr;
    std::mutex mutex;

    // Guarded by mutex
    std::condition_variable wakeup;
    std::map<std::string, std::set<ThreadFrameWindow*>> file_windows;
    bool shutdown = false;
};

class ThreadFrameWindow : public FrameWindow {
  public:
    virtual ~ThreadFrameWindow() {
        if (shared) {
            std::scoped_lock const lock{shared->mutex, decoder_mutex};
            file_iter->second.erase(this);
            if (file_iter->second.empty())
                shared->file_windows.erase(file_iter);
        }
    }

    virtual void set_request(Request const& new_request) {
        std::unique_lock lock{shared->mutex};
        if (!request.keep_decoder) {
            throw std::invalid_argument(fmt::format(
                "Request update for frozen window ({})", file_iter->first
            ));
        }

        if (new_request.start < request.start)
            frames.clear();  // Discard on rewind to keep the prefix invariant.

        // Clear cached frames outside the new window (allowing one past end).
        frames.erase(frames.begin(), frames.lower_bound(new_request.start));
        auto last = frames.lower_bound(new_request.end);
        if (last != frames.end()) frames.erase(std::next(last), frames.end());

        request = new_request;
        lock.unlock();
        shared->wakeup.notify_all();
    }

    virtual Frames loaded() const {
        std::lock_guard const lock{shared->mutex};
        return {frames.begin(), frames.lower_bound(request.end)};  // Trim end
    }

    void open(
        std::shared_ptr<SharedState> shared_state,
        std::unique_ptr<MediaDecoder> media_decoder
    ) {
        decoder = std::move(media_decoder);
        shared = std::move(shared_state);
        auto const filename = decoder->info().filename;
        std::lock_guard const lock{shared->mutex};
        file_iter = shared->file_windows.insert({filename, {}}).first;
        file_iter->second.insert(this);
    }

    static void loader_thread(std::shared_ptr<SharedState> shared) {
        std::unique_lock lock{shared->mutex};
        shared->logger->debug("Frame loader thread running...");
        while (!shared->shutdown) {
            shared->wakeup.wait(lock);
        }
        shared->logger->debug("Frame loader thread ending...");
    }

  private:
    std::mutex decoder_mutex;
    std::unique_ptr<MediaDecoder> decoder;  // Guarded by decoder_mutex
    std::shared_ptr<SharedState> shared;
    std::map<std::string, std::set<ThreadFrameWindow*>>::iterator file_iter;

    // Guarded by shared->mutex
    FrameWindow::Request request = {};
    FrameWindow::Frames frames = {};  // Window prefix, maybe one past the end
};

class ThreadFrameLoader : public FrameLoader {
  public:
    virtual ~ThreadFrameLoader() {
        std::unique_lock lock{shared->mutex};
        if (thread.joinable()) {
            shared->logger->debug("Stopping frame loader...");
            shared->shutdown = true;
            lock.unlock();
            shared->wakeup.notify_all();
            thread.join();
        }
    }

    std::unique_ptr<FrameWindow> open_window(
        std::unique_ptr<MediaDecoder> decoder
    ) {
        auto window = std::make_unique<ThreadFrameWindow>();
        window->open(shared, std::move(decoder));
        return window;
    }

    void start(DisplayDriver* display) {
        shared->logger->info("Launching frame loader...");
        shared->display = display;
        thread = std::thread(&ThreadFrameWindow::loader_thread, shared);
    }

  private:
    std::shared_ptr<SharedState> const shared = std::make_shared<SharedState>();
    std::thread thread;
};

}  // anonymous namespace

std::unique_ptr<FrameLoader> make_frame_loader(DisplayDriver* display) {
    auto loader = std::make_unique<ThreadFrameLoader>();
    loader->start(display);
    return loader;
}

}  // namespace pivid
