#include "frame_loader.h"

#include <iterator>
#include <mutex>
#include <set>
#include <thread>

#include <fmt/core.h>

#include "logging_policy.h"
#include "unix_system.h"

namespace pivid {

namespace {

auto const& loader_logger() {
    static const auto logger = make_logger("loader");
    return logger;
}

class FrameLoaderDef : public FrameLoader {
  public:
    virtual ~FrameLoaderDef() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            DEBUG(logger, "STOP \"{}\"", filename);
            shutdown = true;
            lock.unlock();
            wakeup->set();
            thread.join();
        }
    }

    virtual void set_request(
        IntervalSet const& wanted,
        std::shared_ptr<ThreadSignal> notify
    ) final {
        std::unique_lock lock{mutex};
        this->notify = std::move(notify);

        if (wanted == this->wanted) {
            TRACE(logger, "REQ (same) \"{}\"", filename);
        } else {
            DEBUG(logger, "REQ \"{}\"", filename);
            DEBUG(logger, "  [req] want {}", debug(wanted));

            // Remove no-longer-wanted frames & have-regions
            auto to_erase = out.have;
            for (auto const& want : wanted) {
                // Keep up to one frame before/after, so every instant of each
                // wanted interval has a frame, and so skipahead loads are OK
                auto keep = want;

                auto const begin_have = out.have.overlap_begin(want.begin);
                if (begin_have != out.have.overlap_end(want.begin)) {
                    ASSERT(begin_have->begin <= want.begin);
                    keep.begin = begin_have->begin;
                    auto begin_frame = out.frames.upper_bound(want.begin);
                    if (begin_frame != out.frames.begin()) {
                        --begin_frame;
                        ASSERT(begin_frame->first <= want.begin);
                        keep.begin = std::max(keep.begin, begin_frame->first);
                    }
                }

                auto const end_have = out.have.overlap_begin(want.end);
                if (end_have != out.have.overlap_end(want.end)) {
                    ASSERT(end_have->end >= want.end);
                    keep.end = end_have->end;
                    auto end_frame = out.frames.lower_bound(want.end);
                    if (end_frame != out.frames.end()) {
                        ++end_frame;
                        if (end_frame != out.frames.end()) {
                            ASSERT(end_frame->first >= want.end);
                            keep.end = std::min(keep.end, end_frame->first);
                        }
                    }
                }

                to_erase.erase(keep);
            }

            if (!to_erase.empty()) {
                int nframe = 0;
                for (auto const& erase : to_erase) {
                    out.have.erase(erase);
                    auto const fbegin = out.frames.lower_bound(erase.begin);
                    auto const fend = out.frames.lower_bound(erase.end);
                    nframe += std::distance(fbegin, fend);
                    out.frames.erase(fbegin, fend);
                }
                TRACE(logger, "  [req] del {} ({}fr)", debug(to_erase), nframe);
                TRACE(logger, "  [req] have {}", debug(out.have));
            }

            this->wanted = wanted;
            lock.unlock();
            wakeup->set();
        }
    }

    virtual Content content() const final {
        std::scoped_lock lock{mutex};
        return out;
    }

    void start(
        std::shared_ptr<DisplayDriver> display,
        std::string const& filename,
        std::shared_ptr<UnixSystem> sys,
        std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
    ) {
        this->display = display;
        this->filename = filename;
        this->wakeup = sys->make_signal();
        this->opener = opener;
        DEBUG(logger, "START \"{}\"", filename);
        thread = std::thread(&FrameLoaderDef::loader_thread, this);
    }

    void loader_thread() {
        std::unique_lock lock{mutex};
        TRACE(logger, "starting \"{}\"", filename);

        double last_backtrack = 0.0;
        std::map<double, std::unique_ptr<MediaDecoder>> decoders;
        while (!shutdown) {
            DEBUG(logger, "LOAD \"{}\"", filename);

            auto to_load = wanted;
            to_load.erase({to_load.bounds().begin, 0});
            to_load.erase(out.have);
            if (out.eof) to_load.erase({*out.eof, to_load.bounds().end});

            TRACE(logger, "  have {}", debug(out.have));
            TRACE(logger, "  want {}", debug(wanted));
            TRACE(logger, "  load {}", debug(to_load));

            //
            // Assign decoders to regions of the media to load
            //

            using Assignment = std::tuple<
                Interval, double, std::unique_ptr<MediaDecoder>
            >;
            std::vector<Assignment> to_use;

            // Pass 1: assign decoders that are already well positioned
            auto li = to_load.begin();
            while (li != to_load.end()) {
                auto di = decoders.find(li->begin);
                if (di == decoders.end()) {
                    ++li;
                    continue;
                }

                auto wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                TRACE(
                    logger, "  w={} l={}: d@{:.3f}",
                    debug(*wi), debug(*li), di->first
                );

                to_use.emplace_back(*li, di->first, std::move(di->second));
                decoders.erase(di);
                li = to_load.erase(*wi);
            }

            // Pass 2: reuse other decoders where possible
            li = to_load.begin();
            while (li != to_load.end() && !decoders.empty()) {
                auto di = decoders.upper_bound(li->begin);
                if (di == decoders.end() || di->first >= li->end) {
                    if (di != decoders.begin()) --di;
                }

                auto wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                TRACE(
                    logger, "  w={} l={}: recyc d@{:.3f}",
                    debug(*wi), debug(*li), di->first
                );

                to_use.emplace_back(*li, di->first, std::move(di->second));
                decoders.erase(di);
                li = to_load.erase(*wi);
            }

            // Pass 3: plan to create decoders for remaining needs
            li = to_load.begin();
            while (li != to_load.end()) {
                auto wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                TRACE(logger, "  w={} l={}: new!", debug(*wi), debug(*li));

                to_use.emplace_back(*li, 0.0, nullptr);
                li = to_load.erase(*wi);
            }

            // Remove unused decoders unless positioned at request growth edges
            // TODO: Allow the decoder to be a tiny bit early?
            auto di = decoders.begin();
            while (di != decoders.end()) {
                auto hi = out.have.overlap_begin(di->first);
                if (hi != out.have.begin()) {
                    --hi;
                    if (di->first == hi->end) {
                        TRACE(
                            logger, "  keep d@{:.3f} (h={} end)",
                            di->first, debug(*hi)
                        );
                        ++di;
                        continue;
                    }
                }

                TRACE(logger, "  drop d@{:.3f} (unused)", di->first);
                di = decoders.erase(di);
            }

            // If there's no work, wait for a change in input.
            if (to_use.empty()) {
                TRACE(logger, "  waiting (nothing to load)");
                lock.unlock();
                wakeup->wait();
                lock.lock();
                continue;
            }

            //
            // Do actual blocking work
            // (the lock is released; request state may change!)
            //

            int changes = 0;
            for (auto& [load, orig_pos, decoder] : to_use) {
                if (!wanted.contains(load.begin)) {
                    TRACE(logger, "  obsolete load {}", debug(load));
                    continue;
                }

                double decoder_pos = orig_pos;
                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;
                std::exception_ptr error;
                lock.unlock();

                try {
                    if (!decoder) {
                        TRACE(logger, "  open new decoder");
                        decoder = opener(filename);
                    }

                    // Heuristic threshold for forward-seek vs. read-forward
                    const auto min_seek = std::max(0.050, 2 * last_backtrack);
                    const auto seek_cutoff = load.begin - min_seek;
                    if (decoder_pos < seek_cutoff || decoder_pos >= load.end) {
                        TRACE(
                            logger, "  seek {:.3f}s => {:.3f}s",
                            decoder_pos, load.begin
                        );
                        decoder->seek_before(load.begin);
                        decoder_pos = load.begin;
                    } else if (decoder_pos < load.begin) {
                        TRACE(
                            logger, "  nonseek {:.3f}s => {:.3f}s (<{:.3f}s)",
                            decoder_pos, load.begin, min_seek
                        );
                    }

                    frame = decoder->next_frame();
                    if (frame) image = display->load_image(frame->image);
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    error = std::current_exception();
                    // Pretend as if EOF (frame == nullptr) to avoid looping
                }

                lock.lock();
                if (error) {
                    out.error = error;
                    ++changes;
                }

                if (!frame) {
                    if (!out.eof) {
                        TRACE(logger, "  EOF {:.3f}s (new)", decoder_pos);
                        out.eof = decoder_pos;
                        ++changes;
                    } else if (decoder_pos < *out.eof) {
                        TRACE(
                            logger, "  EOF {:.3f}s (< old {})",
                            decoder_pos, *out.eof
                        );
                        out.eof = decoder_pos;
                        ++changes;
                    } else {
                        TRACE(
                            logger, "  EOF {:.3f}s (>= old {})",
                            decoder_pos, *out.eof
                        );
                    }
                } else {
                    DEBUG(logger, "  d@{:.3f}: {}", decoder_pos, debug(*frame));
                    auto const begin = std::min(decoder_pos, frame->time.begin);
                    if (decoder_pos != orig_pos && begin < decoder_pos) {
                        last_backtrack = decoder_pos - begin;
                        TRACE(logger, "    backtrack {:.3f}s", last_backtrack);
                    }

                    auto const wi = wanted.overlap_begin(begin);
                    if (wi == wanted.overlap_end(frame->time.end)) {
                        TRACE(logger, "    frame old, discarded");
                    } else {
                        TRACE(logger, "    frame overlaps {}", debug(*wi));
                        out.have.insert({begin, frame->time.end});
                        out.frames[frame->time.begin] = std::move(image);
                        ++changes;
                    }

                    decoder_pos = std::max(decoder_pos, frame->time.end);
                }

                // Keep the decoder that was used, with its updated position
                decoders.insert({decoder_pos, std::move(decoder)});
            }

            if (changes) {
                TRACE(logger, "  now have {} ({}ch)", debug(out.have), changes);
                if (notify) notify->set();
            }
        }

        DEBUG(logger, "stopped \"{}\"", filename);
    }

  private:
    // Constant from start to ~
    std::shared_ptr<log::logger> logger = loader_logger();
    std::shared_ptr<DisplayDriver> display;
    std::string filename;
    std::unique_ptr<ThreadSignal> wakeup;
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener;

    std::mutex mutable mutex;
    std::thread thread;

    IntervalSet wanted;
    std::shared_ptr<ThreadSignal> notify;

    bool shutdown = false;
    Content out = {};
};

}  // anonymous namespace

std::unique_ptr<FrameLoader> start_frame_loader(
    std::shared_ptr<DisplayDriver> display,
    std::string const& filename,
    std::shared_ptr<UnixSystem> sys,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
) {
    auto loader = std::make_unique<FrameLoaderDef>();
    loader->start(
        std::move(display), filename, std::move(sys), std::move(opener)
    );
    return loader;
}

}  // namespace pivid
