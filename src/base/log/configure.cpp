#include "base/log/configure.hpp"
#include "base/log/logger.hpp"
#include "base/log/sink.hpp"
#include "base/platform/os.hpp"
#include "base/platform/platform.hpp"


namespace ben_gear::log {

std::filesystem::path default_log_file() {
    return support::data_directory() / "logs" / "bengear.log";
}

bool wants_sink(std::string_view outputs, std::string_view name) {
    // 就地扫描逗号分隔列表，避免全量拷贝和 to_lower 分配
    std::size_t begin = 0;
    const auto n = outputs.size();
    while (begin <= n) {
        auto end = outputs.find(',', begin);
        if (end == std::string_view::npos) end = n;
        auto token = outputs.substr(begin, end - begin);
        // 跳过前导空白
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        // 跳过尾随空白
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
            token.remove_suffix(1);
        }
        // 大小写不敏感比较
        if (token.size() == name.size()) {
            bool match = true;
            for (std::size_t i = 0; i < token.size(); ++i) {
                char a = token[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
                if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
                if (a != b) { match = false; break; }
            }
            if (match) return true;
        }
        if (end == n) break;
        begin = end + 1;
    }
    return false;
}

std::shared_ptr<Logger> make_logger(const config::Settings& settings) {
    SinkList sinks;
    const auto outputs = settings.logging.output.empty() ? std::string("file") : settings.logging.output;
    if (wants_sink(std::string_view(outputs), "stdout")) {
        sinks.push_back(std::make_shared<StdoutSink>());
    }
    if (wants_sink(std::string_view(outputs), "file")) {
        const auto path = settings.logging.file.empty()
                              ? default_log_file()
                              : std::filesystem::path(settings.logging.file.c_str());
        auto max_size = static_cast<size_t>(settings.logging.max_file_size_mb) * 1024 * 1024;
        sinks.push_back(std::make_shared<FileSink>(path, 1000, 64, max_size, settings.logging.max_rotated_files));
    }
    return std::make_shared<Logger>(settings.logging.level, std::move(sinks));
}

void configure(const config::Settings& settings) {
    LogManager::set_logger(make_logger(settings));
}

}  // namespace ben_gear::log
