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

        if (wanted == this->wanted) {
            TRACE(logger, "Req {} (same) {}", debug(wanted), filename);
        } else {
            if (logger->should_log(log_level::debug))
                logger->debug("Req {} {}", debug(wanted), filename);

            // Remove no-longer-wanted frames & have-regions
            auto to_erase = cover;
            for (auto const& want : wanted) {
                auto keep_end = want.end;

                // Keep one loaded frame past the end of each wanted interval
                // (stop at the second frame) to handle skipahead frames.
                auto const tail_have = cover.overlap_begin(want.end);
                if (tail_have != cover.overlap_end(want.end)) {
                    ASSERT(tail_have->end >= want.end);
                    keep_end = tail_have->end;
                    auto tail_frame = frames.lower_bound(want.end);
                    if (tail_frame != frames.end()) {
                        ++tail_frame;
                        if (tail_frame != frames.end()) {
                            ASSERT(tail_frame->first >= want.end);
                            keep_end = std::min(keep_end, tail_frame->first);
                        }
                    }
                }

                to_erase.erase({want.begin, keep_end});
            }

            if (!to_erase.empty()) {
                int nframe = 0;
                for (auto const& erase : to_erase) {
                    cover.erase(erase);
                    auto const fbegin = frames.lower_bound(erase.begin);
                    auto const fend = frames.lower_bound(erase.end);
                    nframe += std::distance(fbegin, fend);
                    frames.erase(fbegin, fend);
                }
                TRACE(logger, "req> dele {} ({}fr)", debug(to_erase), nframe);
                TRACE(logger, "req> have {}", debug(cover));
            }

            this->wanted = wanted;
            lock.unlock();
            wakeup->set();
        }
    }

    virtual Content content() const {
        std::scoped_lock lock{mutex};
        return {frames, cover, eof};
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
            TRACE(logger, "LOAD {}", filename);

            // Don't recycle decoders positioned to handle extension
            std::map<Seconds, std::unique_ptr<MediaDecoder>> save_decoders;
            for (auto const& have : cover) {
                auto iter = decoders.find(have.end);
                if (iter != decoders.end()) {
                    if (!eof || iter->first < *eof) {
                        TRACE(logger, "> keep: {} end decoder", debug(have));
                        save_decoders.insert(decoders.extract(iter));
                    }
                }
            }

            // Find regions needed to load (requested, not loaded, not >EOF)
            auto to_load = wanted;
            if (eof) to_load.erase({*eof, forever});
            to_load.erase(cover);

            TRACE(logger, "> want {}", debug(wanted));
            TRACE(
                logger, "> have {}{}", debug(cover),
                eof ? " eof " + debug(*eof) : ""
            );
            TRACE(logger, "> need {}", debug(to_load));

            // With no regions to load, recycle unneeded decoders & wait
            if (to_load.empty()) {
                if (!decoders.empty())
                    TRACE(logger, "> drop: {} decoders", decoders.size());
                decoders = std::move(save_decoders);
                lock.unlock();
                TRACE(logger, "> waiting (nothing to load)");
                wakeup->wait();
                lock.lock();
                continue;
            }

            int changes = 0;
            for (auto const load : to_load) {
                // Reuse or create a decoder to use for this interval.
                Seconds decoder_pos = {};
                std::unique_ptr<MediaDecoder> decoder;
                auto iter = save_decoders.find(load.begin);
                if (iter != save_decoders.end()) {
                    TRACE(
                        logger, "> deco: {} => use {}",
                        debug(load), debug(iter->first)
                    );
                    decoder_pos = iter->first;
                    decoder = std::move(iter->second);
                    save_decoders.erase(iter);
                } else if (!decoders.empty()) {
                    auto iter = decoders.upper_bound(load.begin);
                    if (iter == decoders.end() || iter->first >= load.end) {
                        if (iter != decoders.begin()) --iter;
                    }
                    TRACE(
                        logger, "> deco: {} => reuse {}",
                        debug(load), debug(iter->first)
                    );
                    decoder_pos = iter->first;
                    decoder = std::move(iter->second);
                    decoders.erase(iter);
                }

                //
                // UNLOCK, seek as needed and read a frame
                //

                lock.unlock();

                auto load_pos = decoder_pos;
                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;
                try {
                    if (!decoder) {
                        TRACE(logger, "> fill: {} => open!", debug(load.begin));
                        decoder = opener(filename);
                        decoder_pos = {};
                    }

                    if (decoder_pos < load.begin || decoder_pos >= load.end) {
                        TRACE(
                            logger, "> seek: {} => {}",
                            debug(decoder_pos), debug(load.begin)
                        );
                        decoder->seek_before(load.begin);
                        decoder_pos = load.begin;
                        load_pos = load.begin;
                    }

                    frame = decoder->next_frame();
                    if (frame) {
                        image = display->load_image(frame->image);
                        load_pos = std::min(load_pos, frame->time.begin);
                        decoder_pos = std::max(decoder_pos, frame->time.end);
                    }
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    // Pretend as if EOF (frame == nullptr) to avoid looping
                }

                //
                // RE-LOCK, check the frame against wanted (may have changed)
                //

                lock.lock();

                if (!frame) {
                    if (!eof || load_pos < *eof) {
                        TRACE(logger, "> EOF (new)");
                        eof = load_pos;
                        wanted.erase({*eof, forever});
                        ++changes;
                    } else if (load_pos == *eof) {
                        TRACE(logger, "> EOF (same)");
                    } else {
                        TRACE(logger, "> EOF (after {})", *eof);
                    }
                } else {
                    if (logger->should_log(log_level::debug))
                        logger->debug("> {}", debug(*frame));

                    auto const want = wanted.overlap_begin(load_pos);
                    if (want == wanted.overlap_end(frame->time.end)) {
                        TRACE(logger, "> frame old, discarded");
                    } else if (frame->time.begin < want->begin) {
                        TRACE(logger, "> frame enters {}", debug(*want));
                        cover.insert({want->begin, frame->time.end});
                        ++changes;
                    } else {
                        TRACE(logger, "> frame inside {}", debug(*want));
                        cover.insert({
                            std::max(want->begin, load_pos),
                            frame->time.end
                        });
                        frames[frame->time.begin] = std::move(image);
                        ++changes;
                    }
                }

                // Keep the decoder that was used, with its updated position
                save_decoders.insert({decoder_pos, std::move(decoder)});
            }

            if (!decoders.empty())
                TRACE(logger, "> drop: {} decoders", decoders.size());
            decoders = std::move(save_decoders);

            if (changes) {
                TRACE(logger, "> have {}", debug(cover));
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

    std::map<Seconds, std::shared_ptr<LoadedImage>> frames;
    IntervalSet<Seconds> cover;  // Regions that are fully loaded
    std::optional<Seconds> eof;  // Where EOF is, if known

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
