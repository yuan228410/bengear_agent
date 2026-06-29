#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ben_gear::log {

inline std::filesystem::path default_log_file() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    const bool tm_ok = localtime_s(&tm, &time_t_now) == 0;
#else
    const bool tm_ok = localtime_r(&time_t_now, &tm) != nullptr;
#endif
    if (!tm_ok) {
        tm = {};
    }
    std::ostringstream name;
    name << "bengear_"
         << std::put_time(&tm, "%Y%m%d") << "_"
         << base::platform::process::current_pid() << ".log";
    return support::data_directory() / "logs" / name.str();
}

inline bool wants_sink(std::string_view outputs, std::string_view name) {
    auto normalized = base::utils::to_lower(std::string(outputs));
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        auto end = normalized.find(',', begin);
        auto token = end == std::string::npos ? normalized.substr(begin) : normalized.substr(begin, end - begin);
        if (base::utils::trim(token) == name) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return false;
}


inline std::string checked_log_string(const container::String& value, const char* name, std::size_t max_size = 4096) {
    const auto* data = value.data();
    const auto size = value.size();
    if (!data) {
        throw std::runtime_error(std::string(name) + " has null data");
    }
    if (size > max_size) {
        throw std::runtime_error(std::string(name) + " has suspicious size: " + std::to_string(size));
    }
    return std::string(data, size);
}

inline std::shared_ptr<Logger> make_logger(const config::Settings& settings) {
    SinkList sinks;
    const auto outputs = settings.logging.output.empty()
        ? std::string("file")
        : checked_log_string(settings.logging.output, "logging.output");
    if (wants_sink(outputs, "stdout")) {
        sinks.push_back(std::make_shared<StdoutSink>());
    }
    if (wants_sink(outputs, "file")) {
        const auto path = settings.logging.file.empty()
            ? default_log_file()
            : std::filesystem::path(checked_log_string(settings.logging.file, "logging.file", 1 << 20));
        auto max_size = static_cast<size_t>(settings.logging.max_file_size_mb) * 1024 * 1024;
        sinks.push_back(std::make_shared<FileSink>(path, 1000, 64, max_size, settings.logging.max_rotated_files));
    }
    if (wants_sink(outputs, "network") && !settings.logging.network_port.empty()) {
        auto host = settings.logging.network_host.empty()
            ? std::string("127.0.0.1")
            : checked_log_string(settings.logging.network_host, "logging.network_host");
        auto port = std::stoi(checked_log_string(settings.logging.network_port, "logging.network_port"));
        auto sink = std::make_shared<TcpServerSink>(host, port);
        sink->start();
        sinks.push_back(std::move(sink));
    }
    auto logger = std::make_shared<Logger>(settings.logging.level, std::move(sinks));
    logger->start();
    return logger;
}

inline void configure(const config::Settings& settings) {
    LogManager::set_logger(make_logger(settings));
}

}  // namespace ben_gear::log
