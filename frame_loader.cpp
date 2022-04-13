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
        std::shared_ptr<SyncFlag> notify
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
        this->display = std::move(display);
        this->filename = filename;
        this->opener = std::move(opener);
        this->wakeup = sys->make_flag();
        this->sys = std::move(sys);
        DEBUG(logger, "START \"{}\"", filename);
        thread = std::thread(&FrameLoaderDef::loader_thread, this);
    }

    void loader_thread() {
        std::unique_lock lock{mutex};
        TRACE(logger, "starting \"{}\"", filename);

        std::map<double, Decoder> decoders;
        while (!shutdown) {
            auto const now = sys->clock();
            DEBUG(logger, "LOAD {} \"{}\"", abbrev_realtime(now), filename);

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

            std::map<double, Decoder> assigned;

            // Pass 1: assign decoders that are already well positioned
            auto li = to_load.begin();
            while (li != to_load.end()) {
                auto di = decoders.find(li->begin);
                if (di == decoders.end()) {
                    ++li;
                    continue;
                }

                auto const wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                TRACE(
                    logger, "  w={} l={}: d@{:.3f}",
                    debug(*wi), debug(*li), di->first
                );

                di = assigned.insert(decoders.extract(di)).position;
                di->second.assignment = *li;
                li = to_load.erase(*wi);
            }

            // Pass 2: reuse other decoders where possible
            li = to_load.begin();
            while (li != to_load.end() && !decoders.empty()) {
                auto di = decoders.upper_bound(li->begin);
                if (di == decoders.end() || di->first >= li->end) {
                    if (di != decoders.begin()) --di;
                }

                auto const wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                TRACE(
                    logger, "  w={} l={}: recyc d@{:.3f}",
                    debug(*wi), debug(*li), di->first
                );

                di = assigned.insert(decoders.extract(di)).position;
                di->second.assignment = *li;
                li = to_load.erase(*wi);
            }

            // Pass 3: request decoder creation for remaining needs
            li = to_load.begin();
            while (li != to_load.end()) {
                auto const wi = wanted.overlap_begin(li->begin);
                ASSERT(wi != wanted.end());
                DEBUG(logger, "  w={} l={}: new!", debug(*wi), debug(*li));

                assigned[li->begin].assignment = *li;
                li = to_load.erase(*wi);
            }

            // Shut down unused decoders that have aged out
            auto di = decoders.begin();
            while (di != decoders.end()) {
                di->second.use_time = std::min(di->second.use_time, now);
                double const age = now - di->second.use_time;

                // TODO make a configurable age cutoff
                double const age_cutoff = 1.0;
                if (age > age_cutoff) {
                    DEBUG(
                        logger, "  drop d@{:.3f} ({:.3f}s old > {:.3f}s)",
                        di->first, age, age_cutoff
                    );
                    di = decoders.erase(di);
                } else {
                    TRACE(
                        logger, "  keep d@{:.3f} ({:.3f}s old <= {:.3f}s)",
                        di->first, age, age_cutoff
                    );
                    ++di;
                }
            }

            //
            // Do actual blocking work
            // (the lock is released here; request state may change!)
            //

            // If there's no work, wait for a change in input.
            if (assigned.empty()) {
                DEBUG(logger, "  waiting (nothing to load)");
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            int changes = 0;
            while (!assigned.empty()) {
                auto node = assigned.extract(assigned.begin());
                auto const& load = node.mapped().assignment;
                if (!wanted.contains(load.begin)) {
                    TRACE(logger, "  obsolete load {}", debug(load));
                    continue;
                }

                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> loaded;
                std::exception_ptr error;
                lock.unlock();

                try {
                    node.mapped().use_time = now;
                    if (!node.mapped().decoder) {
                        TRACE(logger, "  open new decoder");
                        node.mapped().decoder = opener(filename);
                        node.key() = 0.0;
                    }

                    // Heuristic threshold for forward-seek vs. read-forward
                    const auto seek_cutoff = load.begin - std::max(
                        0.050, 2 * node.mapped().backtrack
                    );
                    if (node.key() < seek_cutoff || node.key() >= load.end) {
                        TRACE(
                            logger, "  seek {:.3f}s => {:.3f}s",
                            node.key(), load.begin
                        );
                        node.mapped().decoder->seek_before(load.begin);
                        node.key() = load.begin;
                        node.mapped().backtrack = 0.0;
                    } else if (node.key() < load.begin) {
                        TRACE(
                            logger, "  nonseek {:.3f}s (>{:.3f}s) => {:.3f}s",
                            node.key(), seek_cutoff, load.begin
                        );
                    }

                    frame = node.mapped().decoder->next_frame();
                    if (frame) loaded = display->load_image(frame->image);
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    error = std::current_exception();
                    frame.reset();  // Treat as EOF to avoid looping
                }

                lock.lock();
                if (error) {
                    out.error = error;
                    ++changes;
                }

                if (!frame) {
                    double const eof = node.key();
                    if (!out.eof) {
                        DEBUG(logger, "  EOF {:.3f}s (new)", eof);
                        out.eof = eof;
                        ++changes;
                    } else if (eof < *out.eof) {
                        DEBUG(logger, "  EOF {:.3f}s < old={}", eof, *out.eof);
                        out.eof = eof;
                        ++changes;
                    } else {
                        TRACE(logger, "  EOF {:.3f}s >= old={}", eof, *out.eof);
                    }
                } else {
                    DEBUG(logger, "  d@{:.3f}: {}", node.key(), debug(*frame));
                    double const backtrack = node.key() - frame->time.begin;
                    if (backtrack > node.mapped().backtrack) {
                        node.mapped().backtrack = backtrack;
                        TRACE(logger, "    backtrack {:.3f}s", backtrack);
                    }

                    auto const begin = std::min(node.key(), frame->time.begin);
                    auto const wi = wanted.overlap_begin(begin);
                    if (wi == wanted.overlap_end(frame->time.end)) {
                        TRACE(logger, "    frame old, discarded");
                    } else {
                        TRACE(logger, "    frame overlaps {}", debug(*wi));
                        out.have.insert({begin, frame->time.end});
                        out.frames[frame->time.begin] = std::move(loaded);
                        ++changes;
                    }

                    node.key() = frame->time.end;
                }

                // Keep the decoder that was used, with its updated position
                decoders.insert(std::move(node));
            }

            if (changes) {
                TRACE(logger, "  now have {} ({}ch)", debug(out.have), changes);
                if (notify) notify->set();
            }
        }

        DEBUG(logger, "stopped \"{}\"", filename);
    }

  private:
    struct Decoder {
        std::unique_ptr<MediaDecoder> decoder;
        Interval assignment;
        double backtrack = 0.0;
        double use_time = 0.0;
    };

    // Constant from start to ~
    std::shared_ptr<log::logger> logger = loader_logger();
    std::shared_ptr<DisplayDriver> display;
    std::string filename;
    std::shared_ptr<UnixSystem> sys;
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener;
    std::unique_ptr<SyncFlag> wakeup;

    std::mutex mutable mutex;
    std::thread thread;

    IntervalSet wanted;
    std::shared_ptr<SyncFlag> notify;

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
