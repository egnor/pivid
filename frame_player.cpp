#include "frame_player.h"

#include <condition_variable>
#include <mutex>
#include <thread>

#include <fmt/chrono.h>
#include <fmt/core.h>

#include "logging_policy.h"

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& player_logger() {
    static const auto logger = make_logger("player");
    return logger;
}

class ThreadFramePlayer : public FramePlayer {
  public:
    ThreadFramePlayer() {}

    virtual ~ThreadFramePlayer() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            logger->debug("Stopping frame player...");
            shutdown = true;
            wakeup.notify_all();
            lock.unlock();
            thread.join();
        }
    }

    virtual void set_timeline(Timeline timeline) {
        std::unique_lock const lock{mutex};
        this->timeline = std::move(timeline);
        wakeup.notify_all();
    }

    virtual Timeline::key_type last_shown() const {
        std::lock_guard const lock{mutex};
        return shown;
    }

    void start(
        std::shared_ptr<UnixSystem> sys,
        DisplayDriver* driver,
        uint32_t connector_id,
        DisplayMode mode
    ) {
        logger->info("Launching frame player...");
        thread = std::thread(
            &ThreadFramePlayer::run,
            this,
            std::move(sys),
            driver,
            connector_id,
            std::move(mode)
        );
    }

    void run(
        std::shared_ptr<UnixSystem> sys,
        DisplayDriver* driver,
        uint32_t connector_id,
        DisplayMode mode
    ) {
        using namespace std::chrono_literals;
        logger->debug("Frame player thread running...");
        std::unique_lock lock{mutex};
        while (!shutdown) {
            logger->trace("Frame player iteration...");
            auto const now = sys->steady_time();
            auto const done = driver->update_done_yet(connector_id);
            if (!done) {
                logger->trace("> (update pending, waiting 5ms)");
                auto const try_again = now + 5ms;
                sys->wait_until(try_again, &wakeup, &lock);
                continue;
            }

            auto show = timeline.upper_bound(now);
            if (show != timeline.begin()) {
                auto before = show;
                --before;
                if (before->first > shown) show = before;
            }

            if (logger->should_log(log_level::warn)) {
                for (auto s = timeline.upper_bound(shown); s != show; ++s) {
                    auto const age = now - s->first;
                    logger->warn(
                        "Skip frame sched={:.3f}s ({}ms old)",
                        s->first.time_since_epoch() / 1.0s, age / 1ms
                    );
                }
            }

            if (show == timeline.end()) {
                logger->trace("> (no more frames, waiting for wakeup)");
                wakeup.wait(lock);
                continue;
            }

            if (show->first > now) {
                if (logger->should_log(log_level::trace)) {
                    auto const delay = show->first - now;
                    logger->trace("> (waiting {}ms for frame)", delay / 1ms);
                }
                sys->wait_until(show->first, &wakeup, &lock);
                continue;
            }

            driver->update(connector_id, mode, show->second);
            shown = show->first;

            if (logger->should_log(log_level::debug)) {
                auto const lag = now - shown;
                logger->debug(
                    "Show frame sched={:.3f}s ({}ms old)",
                    shown.time_since_epoch() / 1.0s, lag / 1ms
                );
            }
        }
    }

  private:
    // Constant from start to ~
    std::shared_ptr<log::logger> const logger = player_logger();
    std::thread thread;
    std::mutex mutable mutex;

    // Guarded by mutex
    bool shutdown = false;
    std::condition_variable wakeup;
    Timeline timeline;
    Timeline::key_type shown = {};
};

}  // anonymous namespace

std::unique_ptr<FramePlayer> start_frame_player(
    std::shared_ptr<UnixSystem> sys,
    DisplayDriver* driver,
    uint32_t connector_id,
    DisplayMode mode
) {
    auto player = std::make_unique<ThreadFramePlayer>();
    player->start(std::move(sys), driver, connector_id, std::move(mode));
    return player;
}

}  // namespace pivid
