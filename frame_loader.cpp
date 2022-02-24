#include "frame_loader.h"

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
    std::shared_ptr<ThreadSignal> wakeup = make_signal();
    DisplayDriver* display = nullptr;

    // Guarded by mutex
    std::mutex mutex;
    std::map<std::string, std::set<ThreadFrameWindow*>> by_file;
    std::multimap<double, ThreadFrameWindow*> by_priority;
    bool shutdown = false;
};

class ThreadFrameWindow : public FrameWindow {
  public:
    virtual ~ThreadFrameWindow() {
        if (shared) {
            std::scoped_lock const lock{shared->mutex, decoder_mutex};
            file_iter->second.erase(this);
            if (file_iter->second.empty())
                shared->by_file.erase(file_iter);
            if (priority_iter != shared->by_priority.end())
                shared->by_priority.erase(priority_iter);
        }
    }

    virtual void set_request(Request const& new_request) {
        std::unique_lock lock{shared->mutex};
        if (shared->logger->should_log(log_level::trace)) {
            shared->logger->trace("\"{}\":", file_iter->first);
            shared->logger->trace("> request {}", debug(new_request));
        }

        if (request.freeze) {
            throw std::invalid_argument(fmt::format(
                "\"{}\" frozen window updated", file_iter->first
            ));
        }

        if (new_request.begin < request.begin) {
            loaded.clear();  // Discard on rewind to keep the prefix invariant.
            loaded_eof = false;
        }

        // Clear cached frames outside the new window (allowing one past end).
        loaded.erase(loaded.begin(), loaded.lower_bound(new_request.begin));
        auto last = loaded.lower_bound(new_request.end);
        if (last != loaded.end()) {
            loaded.erase(std::next(last), loaded.end());
            loaded_eof = false;
        }

        request = new_request;
        update_priority(lock);
        if (priority_iter != shared->by_priority.end()) {
            lock.unlock();
            shared->wakeup->set();
        }
    }

    virtual Results results() const {
        std::lock_guard const lock{shared->mutex};

        auto const begin = loaded.begin();
        auto const end = loaded.end();
        if (end != begin) {
            auto const last = std::prev(end);
            if (last->first >= request.end) {
                Results out = {};
                out.frames = {begin, last};
                out.filled = true;
                out.at_eof = false;
                return out;
            }
        }

        Results out = {};
        out.frames = {begin, end};
        out.filled = false;
        out.at_eof = loaded_eof;
        return out;
    }

    void open(
        std::shared_ptr<SharedState> shared_state,
        std::unique_ptr<MediaDecoder> media_decoder
    ) {
        auto const filename = media_decoder->info().filename;
        decoder = std::move(media_decoder);
        shared = std::move(shared_state);

        std::lock_guard const lock{shared->mutex};
        shared->logger->debug("Window \"{}\" opening...", filename);
        file_iter = shared->by_file.insert({filename, {}}).first;
        file_iter->second.insert(this);
        priority_iter = shared->by_priority.end();
    }

    static void loader_thread(std::shared_ptr<SharedState> shared) {
        std::unique_lock lock{shared->mutex};
        auto const& logger = shared->logger;
        logger->debug("Frame loader thread running...");
        while (!shared->shutdown) {
            if (shared->by_priority.empty()) {
                lock.unlock();
                shared->wakeup->wait();
                lock.lock();
                continue;
            }

            // Find the window with the highest priority request.
            auto const [prio, best] = *shared->by_priority.rbegin();
            auto const file = best->file_iter->first;
            auto const tail = best->loaded.empty()
                ? best->request.begin : best->loaded.rbegin()->first;

            shared->logger->trace("LOAD \"{}\"", file);

            if (best->loaded_eof) {
                throw std::logic_error(fmt::format(
                    "\"{}\" prio {:.2f}, but at EOF", file, prio
                ));
            }

            if (tail >= best->request.end) {
                throw std::logic_error(fmt::format(
                    "\"{}\" prio {:.2f}, but tail ({}) >= request.end ({})",
                    file, prio, tail, best->request.end
                ));
            }

            // Hold decoder_lock but not shared->lock while seeking/fetching
            std::unique_lock decoder_lock{best->decoder_mutex};
            if (!best->decoder) {
                throw std::logic_error(fmt::format(
                    "\"{}\" prio {:.3f}, but decoder is null",
                    prio, shared->by_priority.rbegin()->first
                ));
            }

            lock.unlock();

            if (tail != std::max(best->decoder_seek, best->decoder_fetch)) {
                if (logger->should_log(log_level::trace)) {
                    logger->trace(
                        "> seek to {:.3} (last={:.3} fetch={:.3})",
                        tail, best->decoder_seek, best->decoder_fetch
                    );
                }
                best->decoder->seek_before(tail);
                best->decoder_fetch = {};
                best->decoder_seek = tail;
            }

            std::optional<MediaFrame> frame = {};
            try {
                frame = best->decoder->next_frame();
                if (frame) best->decoder_fetch = frame->time;
            } catch (std::runtime_error const& e) {
                logger->error("Loading: {}", e.what());
                frame = {};  // Treat error as EOF
            }

            // Release decoder before re-acquiring shared (maintain lock order)
            // so state may have changed, e.g. 'best' may have been deleted!
            decoder_lock.unlock();
            lock.lock();

            if (logger->should_log(log_level::debug) && frame) {
                logger->debug("{}", debug(*frame));
            }

            if (logger->should_log(log_level::trace) && !frame) {
                logger->trace("> got EOF {:.3}", tail);
            }

            // See if there's still demand for the frame that was fetched
            auto const file_iter = shared->by_file.find(file);
            if (file_iter == shared->by_file.end()) {
                logger->trace("> dropped file");
                continue;
            }

            std::shared_ptr<LoadedImage> loaded = {};
            for (auto* w : file_iter->second) {
                auto const debug_req = logger->should_log(log_level::trace)
                    ? debug(w->request) : std::string{};

                auto const w_tail = w->loaded.empty()
                    ? w->request.begin : w->loaded.rbegin()->first;

                if (w->loaded_eof) {
                    logger->trace(">> {}: at eof, skip", debug_req);
                } else if (w_tail >= w->request.end) {
                    logger->trace(">> {}: filled, skip", debug_req);
                } else if (tail > w_tail) {
                    logger->trace(">> {}: beyond, skip", debug_req);
                } else if (!frame) {
                    logger->trace(">> {}: set EOF", debug_req);
                    w->loaded_eof = true;
                    w->update_priority(lock);
                    if (w->request.signal) w->request.signal->set();
                } else if (frame->time < w_tail) {
                    logger->trace(">> {}: before, skip", debug_req);
                } else {
                    logger->trace(">> {}: adding", debug(w->request));
                    if (!loaded)
                        loaded = shared->display->load_image(frame->image);
                    w->loaded[frame->time] = loaded;
                    w->update_priority(lock);
                    if (w->request.signal) w->request.signal->set();
                }
            }
        }

        shared->logger->debug("Frame loader thread ending...");
    }

  private:
    // Pointer is constant from start to ~ (contents guarded by shared->mutex)
    std::shared_ptr<SharedState> shared;

    // Guarded by shared->mutex
    std::map<std::string, std::set<ThreadFrameWindow*>>::iterator file_iter;
    std::multimap<double, ThreadFrameWindow*>::iterator priority_iter;
    Request request = {};
    std::map<Seconds, std::shared_ptr<LoadedImage>> loaded = {};
    bool loaded_eof = false;

    // Guarded by decoder_mutex
    std::mutex decoder_mutex;  // Acquired after shared->mutex
    std::unique_ptr<MediaDecoder> decoder;
    Seconds decoder_seek = {};
    Seconds decoder_fetch = {};

    void update_priority(std::unique_lock<std::mutex> const& lock) {
        if (!lock || lock.mutex() != &shared->mutex)
            throw std::logic_error("update_priority() without lock");

        for (;;) {
            if (shared->logger->should_log(log_level::trace)) {
                auto const frz = request.freeze ? "frozen!" : "loaded!";
                auto const eof = loaded_eof ? " eof" : "";
                if (loaded.empty()) {
                    shared->logger->trace("> {} empty{}", frz, eof);
                } else {
                    shared->logger->trace(
                        "> {} {:.3}~{:.3} ({}f){}",
                        loaded.rbegin()->first < request.end ? "partial" : frz,
                        loaded.begin()->first, loaded.rbegin()->first,
                        loaded.size(), eof
                    );
                }
            }

            // If fully satisfied, remove the window from the priority queue.
            auto tail = loaded.empty() ? request.begin : loaded.rbegin()->first;
            if (loaded_eof || tail >= request.end) {  // Fully satisfied
                if (request.freeze) {
                    std::lock_guard decoder_lock{decoder_mutex};
                    decoder = nullptr;
                }
                if (priority_iter != shared->by_priority.end()) {
                    shared->by_priority.erase(priority_iter);
                    priority_iter = shared->by_priority.end();
                }
                return;
            }

            // Fill from other windows that overlap this one, if possible.
            auto const old_tail = tail;
            for (ThreadFrameWindow* other : file_iter->second) {
                auto const& oreq = other->request;
                if (other != this && oreq.begin <= tail) {
                    auto const begin = other->loaded.upper_bound(tail);
                    auto end = other->loaded.lower_bound(request.end);
                    if (end != other->loaded.end()) ++end;  // One past end
                    if (begin != end) {
                        if (shared->logger->should_log(log_level::trace)) {
                            shared->logger->trace(
                                "> reusing {:.3}~{:.3} ({}f)",
                                begin->first, std::prev(end)->first,
                                std::distance(begin, end)
                            );
                        }
                        loaded.insert(begin, end);
                        tail = (--end)->first;
                    }
                }
            }

            // If nothing was copied, add to the priority queue and return.
            // (Otherwise, loop to check for completion or more overlaps.)
            if (tail == old_tail) {
                auto const fraction = 
                    (request.end - tail) / (request.end - request.begin);
                auto const prio = request.min_priority +
                    (request.max_priority - request.min_priority) *
                    std::min<double>(1.0, fraction);
                shared->logger->trace("> p={:.3f} end={:.3}", prio, tail);

                if (priority_iter == shared->by_priority.end()) {
                    priority_iter = shared->by_priority.insert({prio, this});
                } else if (priority_iter->first != prio) {
                    shared->by_priority.erase(priority_iter);
                    priority_iter = shared->by_priority.insert({prio, this});
                }
                return;
            }
        }
    }
};

class ThreadFrameLoader : public FrameLoader {
  public:
    virtual ~ThreadFrameLoader() {
        std::unique_lock lock{shared->mutex};
        if (thread.joinable()) {
            shared->logger->debug("Stopping frame loader...");
            shared->shutdown = true;
            lock.unlock();
            shared->wakeup->set();
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

std::string debug(FrameWindow::Request const& req) {
    return fmt::format(
        "{:.3}~{:.3} (p={:.1f}/{:.1f}){}",
        req.begin, req.end, req.max_priority, req.min_priority,
        req.freeze ? " [frozen]" : ""
    );
}

}  // namespace pivid
