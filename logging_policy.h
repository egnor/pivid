#pragma once

#include <string>

#include <spdlog/cfg/helpers.h>
#include <spdlog/spdlog.h>

namespace pivid {

inline void configure_logging(std::string config) {
    spdlog::set_pattern("%H:%M:%S.%e %4iu %^%L [%n] %v%$");
    spdlog::cfg::helpers::load_levels(config);
}

inline std::shared_ptr<spdlog::logger> make_logger(char const* name) {
    auto const& s = spdlog::default_logger()->sinks();
    auto logger = std::make_shared<spdlog::logger>(name, s.begin(), s.end());
    spdlog::initialize_logger(logger);
    return logger;
}

}
