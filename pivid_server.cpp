// HTTP server for video control.

#include <memory>
#include <mutex>
#include <thread>
#include <system_error>

#include <CLI/App.hpp>
#include <CLI/Config.hpp>
#include <CLI/Formatter.hpp>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <httplib/httplib.h>

#include "display_output.h"
#include "logging_policy.h"
#include "script_data.h"
#include "script_runner.h"
#include "unix_system.h"

namespace pivid {

static void to_json(nlohmann::json& j, MediaFileInfo const& info) {
    j = {};
    if (!info.filename.empty()) j["filename"] = info.filename;
    if (!info.container_type.empty()) j["container_type"] = info.container_type;
    if (!info.codec_name.empty()) j["codec_name"] = info.codec_name;
    if (!info.pixel_format.empty()) j["pixel_format"] = info.pixel_format;
    if (info.size) j["size"] = {info.size->x, info.size->y};
    if (info.frame_rate) j["frame_rate"] = *info.frame_rate;
    if (info.bit_rate) j["bit_rate"] = *info.bit_rate;
    if (info.duration) j["duration"] = *info.duration;
}

static void to_json(nlohmann::json& j, DisplayMode const& mode) {
    if (!mode.nominal_hz) {
        j = nullptr;
    } else {
        j["size"] = {mode.size.x, mode.size.y};
        j["nominal_hz"] = mode.nominal_hz;
        j["actual_hz"] = mode.actual_hz();
        if (mode.doubling != XY<int>{})
            j["doubling"] = {mode.doubling.x, mode.doubling.y};
    }
}

static void to_json(nlohmann::json& j, DisplayScreen const& screen) {
    j["detected"] = screen.display_detected;
    j["active_mode"] = screen.active_mode;
    j["modes"] = nlohmann::json(screen.modes);
}

namespace {

std::shared_ptr<log::logger> const& server_logger() {
    static const auto logger = make_logger("server");
    return logger;
}

struct ServerContext {
    std::shared_ptr<UnixSystem> sys;
    std::shared_ptr<DisplayDriver> driver;
    std::unique_ptr<ScriptRunner> runner;
    bool trust_network = false;
    int port = 31415;
};

class Server {
  public:
    ~Server() {
        std::unique_lock lock{mutex};
        if (thread.joinable()) {
            DEBUG(logger, "Stopping update thread");
            shutdown = true;
            wakeup->set();
            thread.join();
        }
    }

    void run(ServerContext&& context) {
        using namespace std::placeholders;
        cx = std::move(context);

        http.Get("/media(/.*)", [&](auto const& q, auto& s) {on_media(q, s);});
        http.Get("/screens", [&](auto const& q, auto& s) {on_screens(q, s);});
        http.Get("/quit", [&](auto const& q, auto& s) {on_quit(q, s);});
        http.Post("/play", [&](auto const& q, auto& s) {on_play(q, s);});

        http.set_logger([&](auto const& q, auto const& s) {log_hook(q, s);});
        http.set_exception_handler(
            [&](auto const& q, auto& s, auto& e) {error_hook(q, s, e);}
        );

        DEBUG(logger, "Launching update thread");
        wakeup = cx.sys->make_flag();
        thread = std::thread(&Server::update_thread, this);
        if (cx.trust_network) {
            logger->info("Listening to WHOLE NETWORK on port {}", cx.port);
            http.listen("0.0.0.0", cx.port);
        } else {
            logger->info("Listening to localhost on port {}", cx.port);
            http.listen("localhost", cx.port);
        }
        logger->info("Stopped listening");
    }

    void update_thread() {
        std::unique_lock lock{mutex};
        TRACE(logger, "Starting update thread");

        double last_mono = 0.0;
        while (!shutdown) {
            if (!script) {
                TRACE(logger, "UPDATE (wait for script)");
                lock.unlock();
                wakeup->sleep();
                lock.lock();
                continue;
            }

            ASSERT(script->main_loop_hz > 0.0);
            double const period = 1.0 / script->main_loop_hz;
            double const mono = cx.sys->clock(CLOCK_MONOTONIC);
            if (mono < last_mono + period) {
                TRACE(
                    logger, "UPDATE (sleep {:.3f}s)",
                    last_mono + period - mono
                );
                lock.unlock();
                wakeup->sleep_until(last_mono + period);
                lock.lock();
                continue;
            }

            DEBUG(logger, "UPDATE (m{:.3f}s)", mono);
            last_mono = std::max(last_mono + period, mono - period);
            auto const copy = script;
            lock.unlock();
            cx.runner->update(*copy);
            lock.lock();
        }

        TRACE(logger, "Update thread stopped");
    }

  private:
    // Constant during run
    std::shared_ptr<log::logger> const logger = server_logger();
    ServerContext cx;
    httplib::Server http;
    std::thread thread;
    std::shared_ptr<SyncFlag> wakeup;

    // Guarded by mutex
    std::mutex mutable mutex;
    bool shutdown = false;
    std::shared_ptr<Script const> script;

    void on_media(httplib::Request const& req, httplib::Response& res) {
        nlohmann::json j = {{"req", req.path}};

        try {
            DEBUG(logger, "INFO \"{}\"", std::string(req.matches[1]));
            auto const media_info = cx.runner->file_info(req.matches[1]);
            j["media"] = media_info;
            j["ok"] = true;
        } catch (std::system_error const& exc) {
            if (exc.code() == std::errc::no_such_file_or_directory) {
                res.status = 404;
                j["error"] = exc.what();
            } else {
                throw;
            }
        }

        res.set_content(j.dump(), "application/json");
    }

    void on_play(httplib::Request const& req, httplib::Response& res) {
        auto new_script = std::make_shared<Script>(parse_script(req.body));

        std::unique_lock lock{mutex};
        DEBUG(logger, "PLAY script ({}b)", req.body.size());
        script = std::move(new_script);
        wakeup->set();

        nlohmann::json const j = {{"req", req.path}, {"ok", true}};
        res.set_content(j.dump(), "application/json");
    }

    void on_screens(httplib::Request const&, httplib::Response& res) {
        nlohmann::json j;
        for (auto const& screen : cx.driver->scan_screens())
            to_json(j[screen.connector], screen);

        res.set_content(j.dump(), "application/json");
    }

    void on_quit(httplib::Request const& req, httplib::Response& res) {
        std::unique_lock lock{mutex};
        DEBUG(logger, "STOP");
        http.stop();
        shutdown = true;
        wakeup->set();

        nlohmann::json const j = {{"req", req.path}, {"ok", true}};
        res.set_content(j.dump(), "application/json");
    }

    void log_hook(httplib::Request const& req, httplib::Response const& res) {
        logger->info(
            "[{}] {} {} {}",
            res.status, req.remote_addr, req.method, req.path
        );
    }

    void error_hook(
        httplib::Request const& req, httplib::Response& res, std::exception& exc
    ) {
        res.status = 500;
        nlohmann::json j = {{"req", req.path}, {"error", exc.what()}};
        res.set_content(j.dump(), "application/json");
    }
};

}  // anonymous namespace

extern "C" int main(int const argc, char const* const* const argv) {
    std::string dev_arg;
    std::string log_arg;
    std::string media_root_arg;

    ScriptContext script_cx;
    ServerContext server_cx;

    CLI::App app("Serve HTTP REST API for video playback");
    app.add_option("--dev", dev_arg, "DRM driver /dev file or hardware path");
    app.add_option("--log", log_arg, "Log level/configuration");
    app.add_option("--port", server_cx.port, "TCP port to listen on");
    app.add_option(
        "--media_root", script_cx.root_dir, "Media directory"
    )->required();
    app.add_flag(
        "--trust_network", server_cx.trust_network,
        "Allow non-localhost connections"
    );
    CLI11_PARSE(app, argc, argv);
    configure_logging(log_arg);
    auto const logger = server_logger();

    try {
        server_cx.sys = global_system();
        for (auto const& dev : list_display_drivers(server_cx.sys)) {
            auto const text = debug(dev);
            if (text.find(dev_arg) == std::string::npos) continue;
            server_cx.driver = open_display_driver(server_cx.sys, dev.dev_file);
            break;
        }
        CHECK_RUNTIME(server_cx.driver, "No DRM device for \"{}\"", dev_arg);

        script_cx.sys = server_cx.sys;
        script_cx.driver = server_cx.driver;
        script_cx.file_base = script_cx.root_dir;
        script_cx.default_zero_time = server_cx.sys->clock();

        logger->info("Media root: {}", script_cx.root_dir);
        logger->info("Start: {}", format_realtime(script_cx.default_zero_time));
        server_cx.runner = make_script_runner(std::move(script_cx));

        Server server;
        server.run(std::move(server_cx));
    } catch (std::exception const& e) {
        logger->critical("{}", e.what());
        return 1;
    }

    return 0;
}

}  // namespace pivid
