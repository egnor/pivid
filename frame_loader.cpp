#include "frame_loader.h"

#include <pthread.h>

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
            TRACE(logger, "Stopping reader: {}", short_filename(cx.filename));
            shutdown = true;
            lock.unlock();
            wakeup->set();
            thread.join();
        }
    }

    virtual void set_request(FrameRequest request) final {
        std::unique_lock lock{mutex};
        if (request.wanted == req.wanted) {
            TRACE(logger, "REQ {} (same)", short_filename(cx.filename));
            req = std::move(request);  // Capture options, skip notify
            return;
        }

        req = std::move(request);
        DEBUG(
            logger, "REQ {} {}",
            short_filename(cx.filename), debug(req.wanted)
        );
        TRACE(
            logger, "  [req] idle={:.3f}s scan={:.3f}s",
            req.decoder_idle_time, req.seek_scan_time
        );

        // Remove no-longer-wanted frames & have-regions
        auto to_erase = loaded.coverage;
        for (auto const& want : req.wanted) {
            // Keep up to one frame before/after, so every instant of each
            // wanted interval has a frame, and so skipahead loads are OK
            auto keep = want;

            auto const begin_have = loaded.coverage.overlap_begin(want.begin);
            if (begin_have != loaded.coverage.overlap_end(want.begin)) {
                ASSERT(begin_have->begin <= want.begin);
                keep.begin = begin_have->begin;
                auto begin_frame = loaded.frames.upper_bound(want.begin);
                if (begin_frame != loaded.frames.begin()) {
                    --begin_frame;
                    ASSERT(begin_frame->first <= want.begin);
                    keep.begin = std::max(keep.begin, begin_frame->first);
                }
            }

            auto const end_have = loaded.coverage.overlap_begin(want.end);
            if (end_have != loaded.coverage.overlap_end(want.end)) {
                ASSERT(end_have->end >= want.end);
                keep.end = end_have->end;
                auto end_frame = loaded.frames.lower_bound(want.end);
                if (end_frame != loaded.frames.end()) {
                    ++end_frame;
                    if (end_frame != loaded.frames.end()) {
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
                loaded.coverage.erase(erase);
                auto const fbegin = loaded.frames.lower_bound(erase.begin);
                auto const fend = loaded.frames.lower_bound(erase.end);
                nframe += std::distance(fbegin, fend);
                loaded.frames.erase(fbegin, fend);
            }
            TRACE(logger, "  [req] del {} ({}fr)", debug(to_erase), nframe);
            TRACE(
                logger, "  [req] have {} ({}fr)",
                debug(loaded.coverage), loaded.frames.size()
            );
        }

        lock.unlock();
        wakeup->set();
    }

    virtual LoadedFrames frames() const final {
        std::scoped_lock lock{mutex};
        return loaded;
    }

    virtual MediaFileInfo file_info() const final {
        // TODO: Share decoders with loader_thread somehow?
        auto const decoder = cx.decoder_f(cx.filename);
        MediaFileInfo const info = decoder->file_info();
        TRACE(logger, "FILE INFO {}", debug(info));
        return info;
    }

    void start(FrameLoaderContext context) {
        cx = std::move(context);
        CHECK_ARG(cx.driver, "No driver for FrameLoader");
        CHECK_ARG(!cx.filename.empty(), "Empty filename for FrameLoader");
        if (!cx.sys) cx.sys = global_system();
        if (!cx.decoder_f) cx.decoder_f = open_media_decoder;
        this->wakeup = cx.sys->make_flag();
        DEBUG(logger, "Launching reader: {}", short_filename(cx.filename));
        thread = std::thread(&FrameLoaderDef::loader_thread, this);
    }

    void loader_thread() {
        auto const thread_name = "pivid:" + short_filename(cx.filename);
        pthread_setname_np(pthread_self(), thread_name.substr(0, 15).c_str());
        TRACE(logger, "Starting reader: {}", short_filename(cx.filename));

        std::map<double, Decoder> decoders;
        std::unique_lock lock{mutex};
        while (!shutdown) {
            auto const now = cx.sys->clock();
            DEBUG(
                logger, "LOAD {} want={}",
                short_filename(cx.filename), debug(req.wanted)
            );

            auto to_load = req.wanted;
            to_load.erase({to_load.bounds().begin, 0});
            to_load.erase(loaded.coverage);
            if (loaded.eof) to_load.erase({*loaded.eof, to_load.bounds().end});

            TRACE(
                logger, "  have={} ({}fr)",
                debug(loaded.coverage), loaded.frames.size()
            );
            TRACE(logger, "  load={}", debug(to_load));

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

                auto const wi = req.wanted.overlap_begin(li->begin);
                ASSERT(wi != req.wanted.end());
                TRACE(
                    logger, "  w={} l={}: use d@{:.3f}",
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

                auto const wi = req.wanted.overlap_begin(li->begin);
                ASSERT(wi != req.wanted.end());
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
                auto const wi = req.wanted.overlap_begin(li->begin);
                ASSERT(wi != req.wanted.end());
                DEBUG(logger, "  w={} l={}: new!", debug(*wi), debug(*li));

                assigned[li->begin].assignment = *li;
                li = to_load.erase(*wi);
            }

            // Shut down unused decoders that have aged out
            auto di = decoders.begin();
            while (di != decoders.end()) {
                di->second.use_time = std::min(di->second.use_time, now);
                double const age = now - di->second.use_time;
                if (age > req.decoder_idle_time) {
                    DEBUG(
                        logger, "  drop d@{:.3f} ({:.3f}s old > {:.3f}s)",
                        di->first, age, req.decoder_idle_time
                    );
                    di = decoders.erase(di);
                } else {
                    TRACE(
                        logger, "  keep d@{:.3f} ({:.3f}s old <= {:.3f}s)",
                        di->first, age, req.decoder_idle_time
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
                DEBUG(
                    logger, "  WAIT {} have={} ({}fr)",
                    short_filename(cx.filename),
                    debug(loaded.coverage), loaded.frames.size()
                );
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            int changes = 0;
            while (!assigned.empty()) {
                auto node = assigned.extract(assigned.begin());
                auto const& load = node.mapped().assignment;
                if (!req.wanted.contains(load.begin)) {
                    TRACE(logger, "  obsolete load={}", debug(load));
                    continue;
                }

                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;
                std::exception_ptr error;
                lock.unlock();

                try {
                    node.mapped().use_time = now;
                    if (!node.mapped().decoder) {
                        TRACE(logger, "  open new decoder");
                        node.mapped().decoder = cx.decoder_f(cx.filename);
                        node.key() = 0.0;
                    }

                    // Heuristic threshold for forward-seek vs. read-forward
                    const auto seek_cutoff = load.begin - std::max(
                        req.seek_scan_time, 2 * node.mapped().backtrack
                    );
                    if (node.key() < seek_cutoff || node.key() >= load.end) {
                        DEBUG(
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
                    if (frame && frame->time.begin >= node.key())
                        image = cx.driver->load_image(std::move(frame->image));
                } catch (std::runtime_error const& e) {
                    logger->error("{}", e.what());
                    error = std::current_exception();
                    frame.reset();  // Treat as EOF to avoid looping
                }

                lock.lock();
                if (error) {
                    loaded.error = error;
                    ++changes;
                }

                if (!frame) {
                    double const eof = node.key();
                    if (!loaded.eof) {
                        DEBUG(logger, "  EOF {:.3f}s (new)", eof);
                        loaded.eof = eof;
                        ++changes;
                    } else if (eof < *loaded.eof) {
                        DEBUG(logger, "  EOF {:.3f}s < {}", eof, *loaded.eof);
                        loaded.eof = eof;
                        ++changes;
                    } else {
                        TRACE(logger, "  EOF {:.3f}s >= {}", eof, *loaded.eof);
                    }
                } else {
                    DEBUG(
                        logger, "  d@{:.3f}: {}",
                        node.key(), debug(*frame, image.get())
                    );
                    double const backtrack = node.key() - frame->time.begin;
                    if (backtrack > node.mapped().backtrack) {
                        node.mapped().backtrack = backtrack;
                        TRACE(logger, "    backtrack {:.3f}s", backtrack);
                    }

                    auto const begin = std::min(node.key(), frame->time.begin);
                    auto const wi = req.wanted.overlap_begin(begin);
                    if (wi == req.wanted.overlap_end(frame->time.end)) {
                        TRACE(logger, "    unwanted frame ignored");
                    } else if (!image) {
                        TRACE(
                            logger, "    frame lands in {} but wasn't loaded",
                            debug(*wi)
                        );
                    } else {
                        TRACE(logger, "    frame lands in {}", debug(*wi));
                        loaded.coverage.insert({begin, frame->time.end});
                        loaded.frames[frame->time.begin] = std::move(image);
                        ++changes;
                    }

                    node.key() = frame->time.end;
                }

                // Keep the decoder that was used, with its updated position
                decoders.insert(std::move(node));
            }

            DEBUG(
                logger, "  LOOP {} Δ{} have={} ({}fr)",
                short_filename(cx.filename), changes,
                debug(loaded.coverage), loaded.frames.size()
            );
            if (changes && req.notify) req.notify->set();
        }

        DEBUG(logger, "Stopped reader: {}", short_filename(cx.filename));
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
    FrameLoaderContext cx;
    std::thread thread;
    std::unique_ptr<SyncFlag> wakeup;

    // Guarded by mutex
    std::mutex mutable mutex;
    bool shutdown = false;
    FrameRequest req = {};
    LoadedFrames loaded = {};
};

}  // anonymous namespace

std::unique_ptr<FrameLoader> start_frame_loader(FrameLoaderContext context) {
    auto loader = std::make_unique<FrameLoaderDef>();
    loader->start(std::move(context));
    return loader;
}

}  // namespace pivid
