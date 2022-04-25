#include "frame_player.h"

#include <pthread.h>

#include <mutex>
#include <thread>

#include <fmt/core.h>

#include "logging_policy.h"

namespace pivid {

namespace {

auto const& player_logger() {
    static const auto logger = make_logger("player");
    return logger;
}

class FramePlayerDef : public FramePlayer {
  public:
    virtual ~FramePlayerDef() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            DEBUG(logger, "Stopping frame player...");
            shutdown = true;
            lock.unlock();
            wakeup->set();
            thread.join();
        }
    }

    virtual void set_timeline(
        Timeline timeline,
        std::shared_ptr<SyncFlag> notify
    ) final {
        std::unique_lock lock{mutex};

        // Avoid thread wakeup if the wakeup schedule doesn't change.
        bool const same_keys = 
            timeline.size() == this->timeline.size() && std::equal(
                timeline.begin(), timeline.end(), this->timeline.begin(),
                [] (auto const& a, auto const& b) { return a.first == b.first; }
            );

        if (timeline.empty()) {
            TRACE(logger, "SET empty");
        } else {
            TRACE(logger, 
                "SET {}f: {}~{} {}",
                timeline.size(),
                abbrev_realtime(timeline.begin()->first),
                abbrev_realtime(timeline.rbegin()->first),
                same_keys ? "[same]" : "[diff]"
            );
        }

        this->timeline = std::move(timeline);
        this->notify = std::move(notify);
        if (!this->timeline.empty() && !same_keys) {
            lock.unlock();
            wakeup->set();
        }
    }

    virtual double last_shown() const final {
        std::scoped_lock const lock{mutex};
        return shown;
    }

    void start(
        std::shared_ptr<DisplayDriver> driver,
        uint32_t screen_id,
        std::shared_ptr<UnixSystem> sys
    ) {
        logger->info("Launching frame player (s={})...", screen_id);
        wakeup = sys->make_flag();
        thread = std::thread(
            &FramePlayerDef::player_thread,
            this,
            std::move(driver),
            screen_id,
            std::move(sys)
        );
    }

    void player_thread(
        std::shared_ptr<DisplayDriver> driver,
        uint32_t screen_id,
        std::shared_ptr<UnixSystem> sys
    ) {
        auto const thread_name = fmt::format("pivid:play:s={}", screen_id);
        pthread_setname_np(pthread_self(), thread_name.substr(0, 15).c_str());
        DEBUG(logger, "Frame player thread running (s={})...", screen_id);

        double expect_done = 0.0;
        std::unique_lock lock{mutex};
        while (!shutdown) {
            if (timeline.empty()) {
                TRACE(logger, "PLAY (s={}) no frames, sleep", screen_id);
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            TRACE(logger, 
                "PLAY (s={}) {}f {}~{}",
                screen_id, timeline.size(),
                abbrev_realtime(timeline.begin()->first),
                abbrev_realtime(timeline.rbegin()->first)
            );

            auto const now = sys->clock();
            auto show = timeline.upper_bound(now);
            if (show != timeline.begin()) {
                auto before = show;
                --before;
                if (before->first > shown) show = before;
            }

            for (auto s = timeline.upper_bound(shown); s != show; ++s) {
                if (!s->second.layers.empty()) {
                    logger->warn(
                        "Skip (s={}) {}lay {} ({:.3f}s old)",
                        screen_id, s->second.layers.size(),
                        abbrev_realtime(s->first), now - s->first
                    );
                } else {
                    TRACE(
                        logger, "Skip (s={}) *empty* {} ({:.3f}s old)",
                        screen_id, abbrev_realtime(s->first), now - s->first
                    );
                }
                shown = s->first;
            }

            if (show == timeline.end()) {
                TRACE(logger, "  (s={} no more frames, sleep)", screen_id);
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            if (show->first > now) {
                auto const delay = show->first - now;
                TRACE(logger, "  (s={} waiting {:.3f}s)", screen_id, delay);
                lock.unlock();
                wakeup->sleep_until(show->first);
                lock.lock();
                continue;
            }

            auto const done = driver->update_status(screen_id);
            if (!done) {
                if (expect_done && now > expect_done) {
                    logger->warn(
                        "Slow update (s={}): {:.3f}s overdue",
                        screen_id, now - expect_done
                    );
                }
                TRACE(logger, "  (s={} update pending, wait 5ms)", screen_id);
                lock.unlock();
                wakeup->sleep_until(now + 0.005);
                lock.lock();
                continue;
            }

            auto const frame_time = show->first;
            DisplayFrame frame = std::move(show->second);
            auto const layer_count = frame.layers.size();
            lock.unlock();

            try {
                auto const expect_delay = 1.0 / frame.mode.actual_hz();
                driver->update(screen_id, std::move(frame));
                expect_done = sys->clock() + expect_delay;
            } catch (std::runtime_error const& e) {
                logger->error("Display (s={}): {}", screen_id, e.what());
                // Continue as if displayed to avoid looping
            }

            DEBUG(
                logger, "Frame (s={}) {}lay {} ({:.3f}s old)",
                screen_id, layer_count, abbrev_realtime(frame_time),
                now - frame_time
            );

            lock.lock();  // State may have changed!
            shown = frame_time;
            if (notify) notify->set();
        }

        DEBUG(logger, "Frame player thread ending (s={})...", screen_id);
    }

  private:
    // Constant from start to ~
    std::shared_ptr<log::logger> const logger = player_logger();
    std::thread thread;
    std::unique_ptr<SyncFlag> wakeup;

    // Guarded by mutex
    std::mutex mutable mutex;
    bool shutdown = false;
    std::shared_ptr<SyncFlag> notify;
    Timeline timeline;
    double shown = {};
};

}  // anonymous namespace

std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<DisplayDriver> driver,
    uint32_t screen_id,
    std::shared_ptr<UnixSystem> sys
) {
    auto p = std::make_unique<FramePlayerDef>();
    p->start(std::move(driver), screen_id, std::move(sys));
    return p;
}

}  // namespace pivid
