#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string>
#include <string_view>

namespace ben_gear::llm {

struct SseEvent {
    std::string event;
    std::string data;
};

/// 有状态的 SSE 流缓冲器，跨 chunk 缓冲不完整的行，按 SSE 事件边界派发
class SseBuffer {
public:
    /// 输入一个 chunk 的原始数据，返回完整解析出的事件
    template<typename Callback>
    void feed(std::string_view chunk, Callback&& on_event) {
        buffer_.append(chunk);

        std::size_t pos = 0;
        while (pos < buffer_.size()) {
            auto nl = buffer_.find('\n', pos);
            if (nl == std::string::npos) {
                // 当前行未完成，保留在 buffer 中
                break;
            }

            auto line = buffer_.substr(pos, nl - pos);
            // 去 \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) {
                // 空行 = 事件结束
                flush_event(on_event);
            } else if (line.size() > 6 && line.substr(0, 6) == "event:") {
                auto value = line.substr(6);
                if (!value.empty() && value[0] == ' ') value = value.substr(1);
                current_event_ = std::move(value);
            } else if (line.size() > 5 && line.substr(0, 5) == "data:") {
                auto value = line.substr(5);
                if (!value.empty() && value[0] == ' ') value = value.substr(1);
                if (!current_data_.empty()) current_data_ += '\n';
                current_data_ += value;
            }
            // 忽略 id:、retry: 等其他字段

            pos = nl + 1;
        }

        // 保留未处理的部分
        if (pos > 0) {
            buffer_.erase(0, pos);
        }
    }

    /// 输入结束后，刷新可能残留的事件
    template<typename Callback>
    void finish(Callback&& on_event) {
        // 如果 buffer 里还有数据，当作最后一行处理
        if (!buffer_.empty()) {
            auto line = std::move(buffer_);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                if (line.size() > 5 && line.substr(0, 5) == "data:") {
                    auto value = line.substr(5);
                    if (!value.empty() && value[0] == ' ') value = value.substr(1);
                    if (!current_data_.empty()) current_data_ += '\n';
                    current_data_ += value;
                }
            }
            buffer_.clear();
        }
        flush_event(on_event);
    }

private:
    template<typename Callback>
    void flush_event(Callback& on_event) {
        if (!current_event_.empty() || !current_data_.empty()) {
            on_event(SseEvent{std::move(current_event_), std::move(current_data_)});
            current_event_.clear();
            current_data_.clear();
        }
    }

    std::string buffer_;
    std::string current_event_;
    std::string current_data_;
};

}  // namespace ben_gear::llm
