#pragma once

#include "ben_gear/base/utils/string_utils.hpp"

#include <string>
#include <string_view>

namespace ben_gear::log {

enum class Level {
    trace = 0,
    debug,
    info,
    warn,
    error,
    critical,
    off,
};

inline std::string_view level_name(Level level) {
    switch (level) {
        case Level::trace: return "trace";
        case Level::debug: return "debug";
        case Level::info: return "info";
        case Level::warn: return "warn";
        case Level::error: return "error";
        case Level::critical: return "critical";
        case Level::off: return "off";
    }
    return "info";
}

inline Level parse_level(std::string_view value) {
    const auto normalized = base::utils::to_lower(base::utils::trim(value));
    if (normalized == "trace") {
        return Level::trace;
    }
    if (normalized == "debug") {
        return Level::debug;
    }
    if (normalized == "warn" || normalized == "warning") {
        return Level::warn;
    }
    if (normalized == "error") {
        return Level::error;
    }
    if (normalized == "critical" || normalized == "fatal") {
        return Level::critical;
    }
    if (normalized == "off" || normalized == "none") {
        return Level::off;
    }
    return Level::info;
}

}  // namespace ben_gear::log

namespace ben_gear {
using LogLevel = log::Level;
using log::level_name;
using log::parse_level;
}  // namespace ben_gear
