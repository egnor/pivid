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
        this->notify = std::move(notify);

        TRACE(logger, "{}: REQUEST", filename);

        if (wanted == this->wanted) {
            TRACE(logger, "req> want {} (same)", debug(wanted));
        } else {
            TRACE(logger, "req> want {}", debug(wanted));

            // Remove no-longer-wanted frames & done-regions
            auto to_erase = load.done;
            for (auto const& want : wanted) {
                // Preserve the wanted zone plus its "load tail"
                auto keep_end = want.end;
                auto const tail_done = load.done.overlap_begin(want.end);
                if (tail_done != load.done.overlap_end(want.end)) {
                    ASSERT(tail_done->end >= want.end);
                    keep_end = tail_done->end;
                    auto const tail_frame = load.frames.lower_bound(want.end);
                    if (tail_frame != load.frames.end()) {
                        ASSERT(tail_frame->first >= want.end);
                        keep_end = std::min(keep_end, tail_frame->first);
                    }
                }

                to_erase.erase({want.begin, keep_end});
            }

            if (!to_erase.empty()) {
                for (auto const& erase : to_erase) {
                    load.done.erase(erase);
                    load.frames.erase(
                        load.frames.lower_bound(erase.begin),
                        load.frames.lower_bound(erase.end)
                    );
                }
                TRACE(logger, "req> erased {}", debug(to_erase));
                TRACE(logger, "req> done   {}", debug(load.done));
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

        std::map<Seconds, std::unique_ptr<MediaDecoder>> decoders;
        while (!shutdown) {
            TRACE(logger, "{}: LOAD", filename);

            // Don't recycle decoders positioned to handle extension
            std::map<Seconds, std::unique_ptr<MediaDecoder>> save_decoders;
            for (auto const& done : load.done) {
                auto iter = decoders.find(done.end);
                if (iter != decoders.end()) {
                    TRACE(logger, "> keep end decoder {}", debug(done));
                    save_decoders.insert(decoders.extract(iter));
                }
            }

            // Find regions needed to load (requested, not loaded, not >EOF)
            auto needed = wanted;
            if (load.eof) needed.erase({*load.eof, forever});
            needed.erase(load.done);

            TRACE(logger, "> want {}", debug(wanted));
            TRACE(logger, "> load {}", debug(load.done));
            TRACE(logger, "> need {}", debug(needed));

            // If no regions are needed, recycle unneeded decoders & wait
            if (needed.empty()) {
                if (!decoders.empty())
                    TRACE(logger, "> drop! {} decoders", decoders.size());
                decoders = std::move(save_decoders);
                lock.unlock();
                TRACE(logger, "> waiting (nothing needed)");
                wakeup->wait();
                lock.lock();
                continue;
            }

            int changes = 0;
            for (auto const need : needed) {
                // Reuse or create a decoder to use for this interval.
                Seconds decoder_pos = {};
                std::unique_ptr<MediaDecoder> decoder;
                auto iter = save_decoders.find(need.begin);
                if (iter != save_decoders.end()) {
                    TRACE(
                        logger, "> needs {} => use {}",
                        debug(need), debug(iter->first)
                    );
                    decoder_pos = iter->first;
                    decoder = std::move(iter->second);
                    save_decoders.erase(iter);
                } else {
                    auto iter = decoders.upper_bound(need.begin);
                    if (iter != decoders.begin()) --iter;
                    if (iter != decoders.end()) {
                        TRACE(
                            logger, "> needs {} => reuse {}",
                            debug(need), debug(iter->first)
                        );
                        decoder_pos = iter->first;
                        decoder = std::move(iter->second);
                        decoders.erase(iter);
                    }
                }

                //
                // UNLOCK, seek as needed and read a frame
                //

                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;

                lock.unlock();

                try {
                    if (!decoder) {
                        TRACE(logger, "> needs {} => open!", debug(need.begin));
                        decoder = opener(filename);
                        decoder_pos = {};
                    }

                    if (need.begin != decoder_pos) {
                        TRACE(
                            logger, "> seek! {} => {}",
                            debug(decoder_pos), debug(need.begin)
                        );
                        decoder->seek_before(need.begin);
                        decoder_pos = need.begin;
                    }

                    frame = decoder->next_frame();
                    if (frame) {
                        image = display->load_image(frame->image);
                        decoder_pos = std::max(decoder_pos, frame->time.end);
                    }
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    // Pretend as if EOF to avoid looping
                }

                //
                // RE-LOCK, check the frame against wanted (may have changed)
                //

                lock.lock();

                if (!frame) {
                    if (!load.eof || need.begin < *load.eof) {
                        TRACE(logger, "> EOF (new)");
                        load.eof = need.begin;
                        wanted.erase({*load.eof, forever});
                        ++changes;
                    } else if (need.begin == *load.eof) {
                        TRACE(logger, "> EOF (same)");
                    } else {
                        TRACE(logger, "> EOF (after {})", *load.eof);
                    }
                } else {
                    auto const overlap = wanted.overlap_begin(need.begin);
                    if (overlap == wanted.overlap_end(frame->time.end)) {
                        TRACE(logger, "> obsolete! {}", debug(frame->time));
                    } else if (overlap->begin > frame->time.begin) {
                        TRACE(
                            logger, "> load! {} enters {}",
                            debug(frame->time), debug(*overlap)
                        );
                        load.done.insert({overlap->begin, frame->time.end});
                        ++changes;
                    } else {
                        TRACE(
                            logger, "> load! {} within {}",
                            debug(frame->time), debug(*overlap)
                        );
                        load.done.insert(frame->time);
                        load.frames[frame->time.begin] = std::move(image);
                        ++changes;
                    }
                }

                // Keep the decoder that was used, with its updated position
                save_decoders.insert({decoder_pos, std::move(decoder)});
            }

            if (!decoders.empty())
                TRACE(logger, "> drop! {} decoders", decoders.size());
            decoders = std::move(save_decoders);

            if (changes) {
                TRACE(logger, "> done {}", debug(load.done));
                TRACE(logger, "> load pass done ({} changes)", changes);
                if (notify) notify->set();
            } else {
                TRACE(logger, "> load pass done (no changes)");
            }
        }

        logger->debug("{}: Loader thread ending...", filename);
    }

  private:
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
    return fmt::format("{}~{}", debug(interval.begin), debug(interval.end));
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
