// HTTP server for video control.

#include <memory>
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

namespace pivid {

namespace {

std::shared_ptr<log::logger> const& server_logger() {
    static const auto logger = make_logger("server");
    return logger;
}

struct ServerContext {
    std::unique_ptr<ScriptRunner> runner;
    bool trust_network = false;
    int port = 31415;
};

class Server {
  public:
    void run(ServerContext&& context) {
        using namespace std::placeholders;
        cx = std::move(context);

        http.Get("/info(/.*)", std::bind(&Server::on_info, this, _1, _2));

        http.Get("/quitquitquit", std::bind(&Server::on_quit, this, _1, _2));

        http.set_logger(std::bind(&Server::log_hook, this, _1, _2));

        http.set_exception_handler(
            std::bind(&Server::error_hook, this, _1, _2, _3)
        );

        if (cx.trust_network) {
            logger->info("Listening to WHOLE NETWORK on port {}", cx.port);
            http.listen("0.0.0.0", cx.port);
        } else {
            logger->info("Listening to localhost on port {}", cx.port);
            http.listen("localhost", cx.port);
        }
        logger->info("Server stopped!");
    }

  private:
    std::shared_ptr<log::logger> const logger = server_logger();
    ServerContext cx;
    httplib::Server http;

    void on_info(httplib::Request const& req, httplib::Response& res) {
        nlohmann::json j = {{"req", req.path}};

        try {
            auto const info = cx.runner->file_info(req.matches[1]);
            to_json(j["info"], info);
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

    void on_quit(httplib::Request const& req, httplib::Response& res) {
        nlohmann::json const j = {{"req", req.path}, {"ok", true}};
        res.set_content(j.dump(), "application/json");
        logger->debug("Stopping HTTP server...");
        http.stop();
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

    try {
        auto const sys = global_system();
        for (auto const& dev : list_display_drivers(global_system())) {
            auto const text = debug(dev);
            if (text.find(dev_arg) != std::string::npos) {
                script_cx.driver = open_display_driver(sys, dev.dev_file);
                break;
            }
        }

        CHECK_RUNTIME(script_cx.driver, "No DRM device for \"{}\"", dev_arg);
        server_logger()->info("Media root: {}", script_cx.root_dir);
        script_cx.file_base = script_cx.root_dir;
        server_cx.runner = make_script_runner(std::move(script_cx));

        Server server;
        server.run(std::move(server_cx));
    } catch (std::exception const& e) {
        server_logger()->critical("{}", e.what());
        return 1;
    }

    return 0;
}

}  // namespace pivid
