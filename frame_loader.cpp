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
            shared->logger->trace(
                "Set request \"{}\" {}", file_iter->first, debug(new_request)
            );
        }

        if (request.final) {
            throw std::invalid_argument(fmt::format(
                "Finalized window updated ({})", file_iter->first
            ));
        }

        if (new_request.begin < request.begin) {
            frames.clear();  // Discard on rewind to keep the prefix invariant.
            frames_end = new_request.begin;
        }

        // Clear cached frames outside the new window (allowing one past end).
        frames.erase(frames.begin(), frames.lower_bound(new_request.begin));
        auto last = frames.lower_bound(new_request.end);
        if (last != frames.end()) {
            frames.erase(std::next(last), frames.end());
            frames_end = last->first;
        }

        request = new_request;
        update_priority(lock);
        if (priority_iter != shared->by_priority.end()) {
            lock.unlock();
            shared->wakeup.notify_all();
        }
    }

    virtual Frames loaded() const {
        std::lock_guard const lock{shared->mutex};
        return frames;
    }

    virtual Frames::key_type load_progress() const {
        std::lock_guard const lock{shared->mutex};
        return frames_end;
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
                shared->wakeup.wait(lock);
                continue;
            }

            // Find the window with the highest priority request.
            auto* const best = shared->by_priority.rbegin()->second;
            if (best->frames_end >= best->request.end) {
                throw std::logic_error(fmt::format(
                    "\"{}\" prio {:.2f}, but end ({}) >= request.end ({})",
                    best->file_iter->first,
                    shared->by_priority.rbegin()->first,
                    best->frames_end, best->request.end
                ));
            }

            auto const file = best->file_iter->first;
            shared->logger->trace("LOAD \"{}\"", file);

            std::unique_lock decoder_lock{best->decoder_mutex};
            if (!best->decoder) {
                throw std::logic_error(fmt::format(
                    "\"{}\" prio {:.3f}, but decoder is null",
                    best->file_iter->first, shared->by_priority.rbegin()->first
                ));
            }

            // Don't hold shared->lock while seeking/fetching
            auto const fetch_at = best->frames_end;  // Capture before unlock
            lock.unlock();

            if (fetch_at != std::max(best->decoder_seek, best->decoder_fetch)) {
                if (logger->should_log(log_level::trace)) {
                    logger->trace(
                        "> seek {} (last={} fetch={})",
                        fetch_at, best->decoder_seek, best->decoder_fetch
                    );
                }
                best->decoder->seek_before(fetch_at);
                best->decoder_fetch = {};
                best->decoder_seek = fetch_at;
            }

            auto const frame = best->decoder->next_frame();
            best->decoder_fetch = frame->time;

            // Release decoder before re-acquiring shared (maintain lock order)
            // so state may have changed, e.g. 'best' may have been deleted!
            decoder_lock.unlock();
            lock.lock();

            if (logger->should_log(log_level::debug) && frame) {
                logger->debug("{}", debug(*frame));
            }

            if (logger->should_log(log_level::trace) && !frame) {
                logger->trace("> EOF after {}", file, fetch_at);
            }

            // See if there's still demand for the frame that was fetched
            auto const file_iter = shared->by_file.find(file);
            if (file_iter == shared->by_file.end()) {
                logger->trace("> (file dropped)", file);
                continue;
            }

            std::shared_ptr<LoadedImage> loaded = {};
            for (auto* w : file_iter->second) {
                if (w->frames_end >= w->request.end) {
                    if (logger->should_log(log_level::trace))
                        logger->trace("> {} (full)", debug(w->request));
                } else if (fetch_at > w->frames_end) {
                    if (logger->should_log(log_level::trace))
                        logger->trace("> {} (fetch late)", debug(w->request));
                } else if (frame && frame->time <= w->frames_end) {
                    if (logger->should_log(log_level::trace))
                        logger->trace("> {} (frame early)", debug(w->request));
                } else {
                    if (!frame) {
                        if (logger->should_log(log_level::trace))
                            logger->trace("> {} EOF hit", debug(w->request));
                        w->frames_end = eof;
                    } else {
                        if (logger->should_log(log_level::trace))
                            logger->trace("> {} frame hit", debug(w->request));
                        if (!loaded)
                            loaded = shared->display->load_image(frame->image);
                        w->frames[frame->time] = loaded;
                        w->frames_end = frame->time;
                    }

                    w->update_priority(lock);
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
    Frames frames = {};  // Covers a prefix of request, up to one past the end
    Millis frames_end = {};  // The end of loaded frames

    // Guarded by decoder_mutex
    std::mutex decoder_mutex;  // Acquired after shared->mutex
    std::unique_ptr<MediaDecoder> decoder;
    Millis decoder_seek = {};
    Millis decoder_fetch = {};

    void update_priority(std::unique_lock<std::mutex> const& lock) {
        if (!lock || lock.mutex() != &shared->mutex)
            throw std::logic_error("update_priority() without lock");

        for (;;) {
            // If fully satisfied, remove the window from the priority queue.
            if (frames_end >= request.end) {  // Fully satisfied
                if (shared->logger->should_log(log_level::trace)) {
                    shared->logger->trace(
                        "> {} {}f {}~{}",
                        request.final ? "finalized" : "full",
                        frames.size(), frames.begin()->first, frames_end
                    );
                }

                if (request.final) {
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
            auto const old_end = frames_end;
            for (ThreadFrameWindow* other : file_iter->second) {
                auto const& oreq = other->request;
                if (other != this && oreq.begin <= frames_end) {
                    auto const begin = other->frames.upper_bound(frames_end);
                    auto end = other->frames.lower_bound(request.end);
                    if (end != other->frames.end()) ++end;  // One past end
                    if (begin != end) {
                        if (shared->logger->should_log(log_level::trace)) {
                            shared->logger->trace(
                                "> reuse {}f {}~{}",
                                std::distance(begin, end),
                                begin->first, std::prev(end)->first
                            );
                        }
                        frames.insert(begin, end);
                        frames_end = (--end)->first;
                    }
                }
            }

            // If nothing was copied, add to the priority queue and return.
            // (Otherwise, loop to check for completion or more overlaps.)
            if (frames_end == old_end) {
                auto const fraction = 
                    (request.end - frames_end) / (request.end - request.begin);
                auto const prio = request.min_priority +
                    (request.max_priority - request.min_priority) *
                    std::min<double>(1.0, fraction);
                shared->logger->trace("> p={:.3f} end={}", prio, frames_end);

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

std::string debug(FrameWindow::Request const& req) {
    return fmt::format(
        "{}~{} (p={:.1f}/{:.1f}){}",
        req.begin, req.end, req.max_priority, req.min_priority,
        req.final ? " [final]" : ""
    );
}

}  // namespace pivid
