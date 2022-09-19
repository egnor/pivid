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
        logger->info("s{} Launching frame player...", screen_id);
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
        auto const thread_name = fmt::format("pivid:play:s{}", screen_id);
        pthread_setname_np(pthread_self(), thread_name.substr(0, 15).c_str());
        DEBUG(logger, "s{} Frame player thread running...", screen_id);

        std::unique_lock lock{mutex};
        while (!shutdown) {
            if (timeline.empty()) {
                TRACE(logger, "s{} PLAY no frames, sleep", screen_id);
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            TRACE(logger, 
                "s{} PLAY {}f {}~{}",
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
                        "s{} SKIPPING FRAME {}l {} ({:.3f}s old)",
                        screen_id, s->second.layers.size(),
                        abbrev_realtime(s->first), now - s->first
                    );
                } else {
                    TRACE(
                        logger, "s{} skip *empty* {} ({:.3f}s old)",
                        screen_id, abbrev_realtime(s->first), now - s->first
                    );
                }
                shown = s->first;
            }

            if (show == timeline.end()) {
                TRACE(logger, "s{}  (no more frames, sleep)", screen_id);
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            if (show->first > now) {
                auto const delay = show->first - now;
                TRACE(logger, "s{}  (waiting {:.3f}s)", screen_id, delay);
                lock.unlock();
                wakeup->sleep_until(show->first);
                lock.lock();
                continue;
            }

            auto const frame_time = show->first;
            DisplayFrame frame = std::move(show->second);
            auto const layer_count = frame.layers.size();
            lock.unlock();

            try {
                auto const start_time = sys->clock();
                driver->update(screen_id, std::move(frame));
                auto const elapsed_time = sys->clock() - start_time;
                auto const expected_time = 1.0 / frame.mode.actual_hz();
                if (elapsed_time > expected_time + 0.005) {
                    logger->warn(
                        "s{} Slow update: took {:.3f}s, expected {:.3f}s",
                        screen_id, elapsed_time, expected_time
                    );
                }
            } catch (std::runtime_error const& e) {
                logger->error("s{} Display: {}", screen_id, e.what());
                // Continue as if displayed to avoid looping
            }

            DEBUG(
                logger, "s{} Frame {}l {} ({:.3f}s old)",
                screen_id, layer_count, abbrev_realtime(frame_time),
                now - frame_time
            );

            lock.lock();  // State may have changed!
            shown = frame_time;
            if (notify) notify->set();
        }

        DEBUG(logger, "s{} Frame player thread ending...", screen_id);
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
