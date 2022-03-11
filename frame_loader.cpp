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
        IntervalSet<Seconds> const& wanted,
        std::shared_ptr<ThreadSignal> notify
    ) {
        std::unique_lock lock{mutex};
        this->notify = notify;

        // TODO expand wanted to include frame-past-the-end

        if (wanted == this->wanted) {
            TRACE(logger, "{}: request {} (same)", filename, debug(wanted));
        } else {
            TRACE(logger, "{}: request {}", filename, debug(wanted));

            auto to_erase = load.done;
            to_erase.erase(wanted);
            if (!to_erase.empty()) TRACE(logger, "> erase {}", debug(to_erase));
            for (auto const& erase : to_erase) {
                load.done.erase(erase);
                load.frames.erase(
                    load.frames.lower_bound(erase.begin),
                    load.frames.lower_bound(erase.end)
                );
            }

            this->wanted = wanted;
            lock.unlock();
            wakeup->set();
        }
    }

    virtual Loaded loaded() const {
        std::scoped_lock lock{mutex};
        return load;
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
        logger->debug("{}: Loader thread running...", filename);

        std::map<Seconds, Reader> readers;
        while (!shutdown) {
            TRACE(logger, "LOAD {}", filename);

            // Don't recycle readers positioned to handle request extension
            std::map<Seconds, Reader> keep_readers;
            for (auto const& want : wanted) {
                auto iter = readers.find(want.end);
                if (iter != readers.end()) {
                    TRACE(logger, "> keep end reader {}", debug(want));
                    keep_readers.insert(readers.extract(iter));
                }
            }

            // Find regions needed to load (requested, not loaded, not >EOF)
            auto needed = wanted;
            if (load.eof) needed.erase({*load.eof, forever});
            needed.erase(load.done);

            // If no regions are needed, recycle unneeded readers & wait
            if (needed.empty()) {
                const auto nr = readers.size();
                TRACE(logger, "> nothing needed, waiting (drop {}r)", nr);
                readers = std::move(keep_readers);
                lock.unlock();
                wakeup->wait();
                lock.lock();
                continue;
            }

            // Reserve readers positioned exactly fill needed regions
            for (auto const need : needed) {
                auto iter = readers.find(need.begin);
                if (iter != readers.end())
                    keep_readers.insert(readers.extract(iter));
            }

            bool changed = false;
            for (auto const need : needed) {
                // Find the reader to use for this interval.
                // Use an exact "keeper", the best leftover, or a new reader.
                Reader reader;
                auto iter = keep_readers.find(need.begin);
                if (iter != keep_readers.end()) {
                    TRACE(logger, "> reuse {}", debug(need));
                    reader = std::move(iter->second);
                    keep_readers.erase(iter);
                } else {
                    iter = readers.find(need.begin);
                    if (iter != readers.begin()) --iter;
                    if (iter != readers.end()) {
                        const auto t = iter->first;
                        TRACE(logger, "> repos {:.3} => {}", t, debug(need));
                        reader = std::move(iter->second);
                        readers.erase(iter);
                    } else {
                        TRACE(logger, "> open! {}", debug(need));
                        try {
                            reader.decoder = opener(filename);
                        } catch (std::runtime_error const& e) {
                            logger->error("{}", e.what());
                            load.done.insert(need);
                            changed = true;
                            continue;  // Pretend as if loaded to avoid looping
                        }
                    }
                }

                //
                // UNLOCK, seek as needed and read a frame
                //

                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;

                lock.unlock();

                try {
                    auto rt = std::max(reader.last_seek, reader.last_fetch);
                    if (need.begin != rt) {
                        reader.decoder->seek_before(need.begin);
                        reader.last_seek = need.begin;
                        reader.last_fetch = {};
                    }

                    frame = reader.decoder->next_frame();
                    if (frame) {
                        reader.last_fetch = frame->time;
                        image = display->load_image(frame->image);
                    }
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    // Pretend as if EOF to avoid looping
                }

                //
                // RE-LOCK, add this frame if it's useful
                //

                lock.lock();

                if (!frame) {
                    if (!load.eof || need.begin < *load.eof) {
                        TRACE(logger, "> EOF {:.3}", need.begin);
                        load.eof = need.begin;
                        wanted.erase({*load.eof, forever});
                        changed = true;
                    } else {
                        TRACE(logger, "> EOF {:.3} (after)", need.begin);
                    }
                } else if (frame->time < need.begin) {
                    TRACE(
                        logger, "> fr {:.3} but < {}", frame->time, debug(need)
                    );
                } else if (!wanted.contains(need.begin)) {
                    TRACE(
                        logger, "> fr {:.3} but {} now unwanted",
                        frame->time, debug(need)
                    );
                } else if (load.done.contains(need.begin)) {
                    TRACE(
                        logger, "> fr {:.3} but {} already done",
                        frame->time, debug(need)
                    );
                } else {
                    TRACE(logger, "> fr {:.3}~{:.3}", need.begin, frame->time);
                    load.done.insert({need.begin, frame->time});
                    load.frames.insert({frame->time, std::move(image)});
                    auto rt = std::max(reader.last_seek, reader.last_fetch);
                    keep_readers.insert({rt, std::move(reader)});
                    changed = true;
                }
            }

            if (!readers.empty())
                TRACE(logger, "> load pass done, drop {}r", readers.size());
            readers = std::move(keep_readers);
            if (changed && notify) notify->set();
        }

        logger->debug("{}: Loader thread ending...", filename);
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

    IntervalSet<Seconds> wanted;
    std::shared_ptr<ThreadSignal> notify;
    Loaded load;

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

std::string debug(Interval<Seconds> const& interval) {
    return fmt::format("{:.3}~{:.3}", interval.begin, interval.end);
}

std::string debug(IntervalSet<Seconds> const& set) {
    std::string out = "{";
    for (auto const& interval : set) {
        if (out.size() > 1) out += ", ";
        out += debug(interval);
    }
    return out + "}";
}

}  // namespace pivid
