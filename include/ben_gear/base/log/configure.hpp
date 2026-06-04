#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace ben_gear::log {

inline std::filesystem::path default_log_file() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    {
        static std::mutex time_mutex;
        std::lock_guard lock(time_mutex);
        tm = *std::localtime(&time_t_now);
    }
    std::ostringstream name;
    name << "bengear_"
         << std::put_time(&tm, "%Y%m%d") << "_"
         << base::platform::process::current_pid() << ".log";
    return std::filesystem::current_path() / "logs" / name.str();
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

inline std::shared_ptr<Logger> make_logger(const config::Settings& settings) {
    SinkList sinks;
    const auto outputs = settings.logging.output.empty() ? container::String("file") : settings.logging.output;
    if (wants_sink(std::string_view(outputs), "stdout")) {
        sinks.push_back(std::make_shared<StdoutSink>());
    }
    if (wants_sink(std::string_view(outputs), "file")) {
        const auto path = settings.logging.file.empty() ? default_log_file() : std::filesystem::path(settings.logging.file);
        auto max_size = static_cast<size_t>(settings.logging.max_file_size_mb) * 1024 * 1024;
        sinks.push_back(std::make_shared<FileSink>(path, 1000, 64, max_size, settings.logging.max_rotated_files));
    }
    if (wants_sink(std::string_view(outputs), "network") && !settings.logging.network_port.empty()) {
        auto host = settings.logging.network_host.empty() ? "127.0.0.1" : std::string(settings.logging.network_host.c_str());
        auto port = std::stoi(std::string(settings.logging.network_port));
        sinks.push_back(std::make_shared<TcpServerSink>(host, port));
    }
    return std::make_shared<Logger>(settings.logging.level, std::move(sinks));
}

inline void configure(const config::Settings& settings) {
    LogManager::set_logger(make_logger(settings));
}

}  // namespace ben_gear::log
