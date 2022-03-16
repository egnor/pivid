// Logging-related utilities for configuring spdlog the way we like it
// (see github.com/gabime/spdlog).

#pragma once

#include <stdexcept>
#include <string>

#include <spdlog/cfg/helpers.h>
#include <spdlog/spdlog.h>

namespace pivid {

// Convenience aliases
namespace log = ::spdlog;
using log_level = ::spdlog::level::level_enum;

#define TRACE(l, ...) \
    do { if (l->should_log(log_level::trace)) l->trace(__VA_ARGS__); } while (0)

#define DEBUG(l, ...) \
    do { if (l->should_log(log_level::debug)) l->debug(__VA_ARGS__); } while (0)

#define ASSERT(f) \
    do { if (!(f)) throw std::logic_error("ASSERT fail: " #f); } while (0)

// Configures the logger output format with our preferred pattern.
// Sets log levels based on a string, typically a command line --log arg,
// to allow "--log=info,display=trace,media=debug" type parameters.
inline void configure_logging(std::string config) {
    spdlog::set_pattern("%H:%M:%S.%e %6iu %^%L [%n] %v%$");
    spdlog::cfg::helpers::load_levels(config);
}

// Creates a new logger that shares the same "sinks" as the default logger.
// Used to create named loggers for subcomponents that can have their
// log levels adjusted separately but all write in the same output stream.
inline std::shared_ptr<spdlog::logger> make_logger(char const* name) {
    auto const& s = spdlog::default_logger()->sinks();
    auto logger = std::make_shared<spdlog::logger>(name, s.begin(), s.end());
    spdlog::initialize_logger(logger);
    return logger;
}

}
