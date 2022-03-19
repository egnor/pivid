#include "frame_player.h"

#include <mutex>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "logging_policy.h"
#include "thread_signal.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& player_logger() {
    static const auto logger = make_logger("player");
    return logger;
}

class ThreadFramePlayer : public FramePlayer {
  public:
    virtual ~ThreadFramePlayer() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            logger->debug("Stopping frame player...");
            shutdown = true;
            lock.unlock();
            wakeup->set();
            thread.join();
        }
    }

    virtual void set_timeline(
        Timeline timeline,
        std::shared_ptr<ThreadSignal> notify
    ) {
        std::unique_lock lock{mutex};

        // Avoid thread wakeup if the wakeup schedule doesn't change.
        bool const same_keys = 
            timeline.size() == this->timeline.size() && std::equal(
                timeline.begin(), timeline.end(), this->timeline.begin(),
                [] (auto const& a, auto const& b) { return a.first == b.first; }
            );

        if (timeline.empty()) {
            TRACE(logger, "Set timeline empty");
        } else {
            TRACE(logger, 
                "Set timeline {}f {}~{} {}",
                timeline.size(),
                debug(timeline.begin()->first.time_since_epoch()),
                debug(timeline.rbegin()->first.time_since_epoch()),
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

    virtual SteadyTime last_shown() const {
        std::scoped_lock const lock{mutex};
        return shown;
    }

    void start(
        std::shared_ptr<UnixSystem> sys,
        DisplayDriver* driver,
        uint32_t screen_id,
        DisplayMode mode
    ) {
        logger->info("Launching frame player...");
        thread = std::thread(
            &ThreadFramePlayer::player_thread,
            this,
            std::move(sys),
            driver,
            screen_id,
            std::move(mode)
        );
    }

    void player_thread(
        std::shared_ptr<UnixSystem> sys,
        DisplayDriver* driver,
        uint32_t screen_id,
        DisplayMode mode
    ) {
        using namespace std::chrono_literals;
        logger->debug("Frame player thread running...");

        std::unique_lock lock{mutex};
        while (!shutdown) {
            if (timeline.empty()) {
                TRACE(logger, "PLAY (no frames, waiting for wakeup)");
                lock.unlock();
                wakeup->wait();
                lock.lock();
                continue;
            }

            TRACE(logger, 
                "PLAY timeline {}f {}~{}",
                timeline.size(),
                debug(timeline.begin()->first.time_since_epoch()),
                debug(timeline.rbegin()->first.time_since_epoch())
            );

            auto const now = sys->steady_time();
            auto show = timeline.upper_bound(now);
            if (show != timeline.begin()) {
                auto before = show;
                --before;
                if (before->first > shown) show = before;
            }

            for (auto s = timeline.upper_bound(shown); s != show; ++s) {
                logger->warn(
                    "Skip frame sched={} ({} old)",
                    debug(s->first.time_since_epoch()), debug(now - s->first)
                );
                shown = s->first;
            }

            if (show == timeline.end()) {
                TRACE(logger, "> (no more frames, waiting for wakeup)");
                lock.unlock();
                wakeup->wait();
                lock.lock();
                continue;
            }

            if (show->first > now) {
                auto const delay = show->first - now;
                TRACE(logger, "> (waiting {} for frame)", debug(delay));
                lock.unlock();
                wakeup->wait_until(show->first);
                lock.lock();
                continue;
            }

            auto const done = driver->update_done_yet(screen_id);
            if (!done) {
                TRACE(logger, "> (update pending, waiting 5ms)");
                auto const try_again = now + 5ms;
                lock.unlock();
                wakeup->wait_until(try_again);
                lock.lock();
                continue;
            }

            try {
                driver->update(screen_id, mode, show->second);
            } catch (std::runtime_error const& e) {
                logger->error("Display: {}", e.what());
                // Continue as if displayed to avoid looping
            }

            shown = show->first;
            if (notify) notify->set();

            auto const lag = now - shown;
            logger->debug(
                "Show frame sched={} ({} old)",
                debug(shown.time_since_epoch()), debug(lag)
            );
        }

        logger->debug("Frame player thread ending...");
    }

  private:
    // Constant from start to ~
    std::shared_ptr<log::logger> const logger = player_logger();
    std::thread thread;
    std::shared_ptr<ThreadSignal> wakeup = make_signal();

    // Guarded by mutex
    std::mutex mutable mutex;
    bool shutdown = false;
    std::shared_ptr<ThreadSignal> notify;
    Timeline timeline;
    SteadyTime shown = {};
};

}  // anonymous namespace

std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<UnixSystem> sys,
    DisplayDriver* driver,
    uint32_t screen_id,
    DisplayMode mode
) {
    auto player = std::make_unique<ThreadFramePlayer>();
    player->start(std::move(sys), driver, screen_id, std::move(mode));
    return player;
}

}  // namespace pivid
