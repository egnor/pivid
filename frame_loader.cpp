#include "frame_loader.h"

#include <iterator>
#include <mutex>
#include <set>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "logging_policy.h"
#include "thread_signal.h"
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
            DEBUG(logger, "main> STOP {}", filename);
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
            TRACE(logger, "SAME {}", filename);
        } else {
            DEBUG(logger, "REQ  {}", filename);
            DEBUG(logger, "req> want {}", debug(wanted));

            // Remove no-longer-wanted frames & have-regions
            auto to_erase = out.have;
            for (auto const& want : wanted) {
                auto keep_end = want.end;

                // Keep one loaded frame past the end of each wanted interval
                // (stop at the second frame) to handle skipahead frames.
                auto const tail_have = out.have.overlap_begin(want.end);
                if (tail_have != out.have.overlap_end(want.end)) {
                    ASSERT(tail_have->end >= want.end);
                    keep_end = tail_have->end;
                    auto tail_frame = out.frames.lower_bound(want.end);
                    if (tail_frame != out.frames.end()) {
                        ++tail_frame;
                        if (tail_frame != out.frames.end()) {
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
                    out.have.erase(erase);
                    auto const fbegin = out.frames.lower_bound(erase.begin);
                    auto const fend = out.frames.lower_bound(erase.end);
                    nframe += std::distance(fbegin, fend);
                    out.frames.erase(fbegin, fend);
                }
                TRACE(logger, "req> dele {} ({}fr)", debug(to_erase), nframe);
                TRACE(logger, "req> have {}", debug(out.have));
            }

            this->wanted = wanted;
            lock.unlock();
            wakeup->set();
        }
    }

    virtual Content content() const {
        std::scoped_lock lock{mutex};
        return out;
    }

    void start(
        std::shared_ptr<DisplayDriver> display,
        std::string const& filename,
        std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
    ) {
        this->display = display;
        this->filename = filename;
        this->opener = opener;
        DEBUG(logger, "main> START {}", filename);
        thread = std::thread(&FrameLoaderDef::loader_thread, this);
    }

    void loader_thread() {
        std::unique_lock lock{mutex};
        DEBUG(logger, "START {}", filename);

        std::map<Seconds, std::unique_ptr<MediaDecoder>> decoders;
        while (!shutdown) {
            TRACE(logger, "LOAD {}", filename);

            auto to_load = wanted;
            to_load.erase(out.have);
            if (out.eof) to_load.erase({*out.eof, forever});

            TRACE(logger, "> have {}", debug(out.have));
            TRACE(logger, "> want {}", debug(wanted));
            TRACE(logger, "> load {}", debug(to_load));

            //
            // Assign decoders to regions of the media to load
            //

            using Assignment = std::tuple<
                Interval<Seconds>, Seconds, std::unique_ptr<MediaDecoder>
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
                    logger, "> {} ({}) use {}",
                    debug(*wi), debug(*li), debug(di->first)
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
                    logger, "> {} ({}) get {}",
                    debug(*wi), debug(*li), debug(di->first)
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
                TRACE(
                    logger, "> {} ({}) new decoder",
                    debug(*wi), debug(*li)
                );

                to_use.emplace_back(*li, Seconds{}, nullptr);
                li = to_load.erase(*wi);
            }

            // Remove unused decoders unless positioned at request growth edges
            auto di = decoders.begin();
            while (di != decoders.end()) {
                if (out.eof && di->first >= *out.eof) {
                    TRACE(
                        logger, "> drop: {} (>= eof {})",
                        debug(di->first), debug(*out.eof)
                    );
                    di = decoders.erase(di);
                    continue;
                }

                if (out.have.empty() && di->first == Seconds(0)) {
                    TRACE(logger, "> keep: starting decoder");
                    ++di;
                    continue;
                }

                auto hi = out.have.overlap_begin(di->first);
                if (hi != out.have.begin()) {
                    --hi;
                    if (di->first == hi->end) {
                        TRACE(logger, "> keep: {} end decoder", debug(*hi));
                        ++di;
                        continue;
                    }
                }

                TRACE(logger, "> drop: {}", debug(di->first));
                di = decoders.erase(di);
            }

            // If there's no work, wait for a change in input.
            if (to_use.empty()) {
                TRACE(logger, "> waiting (nothing to load)");
                lock.unlock();
                wakeup->wait();
                lock.lock();
                continue;
            }

            //
            // Do actual blocking work
            // (releasing the lock; request state may change!)
            //

            int changes = 0;
            for (auto& [load, orig_pos, decoder] : to_use) {
                if (!wanted.contains(load.begin)) {
                    TRACE(logger, "> load: {} obsolete", debug(load));
                    continue;
                }

                Seconds begin_pos = orig_pos;
                std::optional<MediaFrame> frame;
                std::unique_ptr<LoadedImage> image;
                std::exception_ptr error;
                lock.unlock();

                try {
                    if (!decoder) {
                        TRACE(logger, "> new decoder");
                        decoder = opener(filename);
                    }

                    if (begin_pos < load.begin || begin_pos >= load.end) {
                        TRACE(
                            logger, "> seek: {}: {}",
                            debug(begin_pos), debug(load.begin)
                        );
                        decoder->seek_before(load.begin);
                        begin_pos = load.begin;
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

                Seconds end_pos = begin_pos;
                if (!frame) {
                    if (!out.eof || begin_pos < *out.eof) {
                        TRACE(logger, "> EOF:  {} (new)", debug(begin_pos));
                        out.eof = begin_pos;
                        ++changes;
                    } else if (begin_pos >= *out.eof) {
                        TRACE(
                            logger, "> EOF:  {} (>= old {})",
                            debug(begin_pos), *out.eof
                        );
                    }
                } else {
                    begin_pos = std::min(begin_pos, frame->time.begin);
                    end_pos = std::max(end_pos, frame->time.end);

                    DEBUG(logger, "{}~{}", debug(begin_pos), debug(*frame));

                    auto const wi = wanted.overlap_begin(begin_pos);
                    if (wi == wanted.overlap_end(end_pos)) {
                        TRACE(logger, "> frame old, discarded");
                    } else if (begin_pos < wi->begin) {
                        TRACE(logger, "> frame enters {}", debug(*wi));
                        out.have.insert({wi->begin, end_pos});
                        ++changes;
                    } else {
                        TRACE(logger, "> frame inside {}", debug(*wi));
                        auto const have_begin = std::max(wi->begin, begin_pos);
                        out.have.insert({have_begin, end_pos});
                        out.frames[frame->time.begin] = std::move(image);
                        ++changes;
                    }
                }

                // Keep the decoder that was used, with its updated position
                decoders.insert({end_pos, std::move(decoder)});
            }

            if (changes) {
                TRACE(logger, "> now have {}", debug(out.have));
                TRACE(logger, "> pass done ({} changes)", changes);
                if (notify) notify->set();
            } else {
                TRACE(logger, "> pass done (no changes)");
            }
        }

        DEBUG(logger, "STOP {}", filename);
    }

  private:
    std::shared_ptr<log::logger> logger = loader_logger();
    std::shared_ptr<DisplayDriver> display;
    std::string filename;
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener;

    std::mutex mutable mutex;
    std::thread thread;
    std::shared_ptr<ThreadSignal> wakeup = make_signal();

    IntervalSet<Seconds> wanted;
    std::shared_ptr<ThreadSignal> notify;

    bool shutdown = false;
    Content out = {};
};

}  // anonymous namespace

std::unique_ptr<FrameLoader> start_frame_loader(
    std::shared_ptr<DisplayDriver> display,
    std::string const& filename,
    std::function<std::unique_ptr<MediaDecoder>(std::string const&)> opener
) {
    auto loader = std::make_unique<FrameLoaderDef>();
    loader->start(std::move(display), filename, std::move(opener));
    return loader;
}

}  // namespace pivid
