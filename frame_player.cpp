#include "frame_player.h"

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
        DisplayMode mode,
        std::shared_ptr<UnixSystem> sys
    ) {
        logger->info("Launching frame player...");
        wakeup = sys->make_flag();
        thread = std::thread(
            &FramePlayerDef::player_thread,
            this,
            std::move(driver),
            screen_id,
            std::move(mode),
            std::move(sys)
        );
    }

    void player_thread(
        std::shared_ptr<DisplayDriver> driver,
        uint32_t screen_id,
        DisplayMode mode,
        std::shared_ptr<UnixSystem> sys
    ) {
        DEBUG(logger, "Frame player thread running...");

        double last_update = 0.0;
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
                logger->warn(
                    "Skip (s={}) sch={} ({:.3f}s old)",
                    screen_id, abbrev_realtime(s->first), now - s->first
                );
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
                if (last_update && now - last_update > 1.0 / mode.actual_hz()) {
                    logger->warn(
                        "Slow update: {:.3f}s pending > {:.3f}s refresh",
                        now - last_update, 1.0 / mode.actual_hz()
                    );
                }
                TRACE(logger, "  (s={} update pending, wait 5ms)", screen_id);
                lock.unlock();
                wakeup->sleep_until(now + 0.005);
                lock.lock();
                continue;
            }

            try {
                driver->update(screen_id, mode, show->second);
                last_update = sys->clock();
            } catch (std::runtime_error const& e) {
                logger->error("Display (screen {}): {}", screen_id, e.what());
                // Continue as if displayed to avoid looping
            }

            shown = show->first;
            if (notify) notify->set();

            auto const lag = now - shown;
            DEBUG(
                logger, "Frame (s={}) sch={} ({:.3f}s old)",
                screen_id, abbrev_realtime(shown), lag
            );
        }

        DEBUG(logger, "Frame player thread ending...");
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
    DisplayMode mode,
    std::shared_ptr<UnixSystem> sys
) {
    auto p = std::make_unique<FramePlayerDef>();
    p->start(std::move(driver), screen_id, std::move(mode), std::move(sys));
    return p;
}

}  // namespace pivid
