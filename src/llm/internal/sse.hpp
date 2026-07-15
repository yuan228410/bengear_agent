#pragma once


#include <string_view>

namespace ben_gear::llm {

namespace container = base::container;

/// SSE 事件（使用 std::string，SSO 优化短字符串）
struct SseEvent {
    std::string event;
    std::string data;
};

/// 有状态的 SSE 流缓冲器，跨 chunk 缓冲不完整的行，按 SSE 事件边界派发
/// 使用 std::string 替代 std::string，短字符串（<=23字节）零堆分配
/// 使用 read_offset_ 替代频繁 erase，避免每行 shift 导致的 O(N²) 开销
class SseBuffer {
public:
    /// 输入一个 chunk 的原始数据，返回完整解析出的事件
    template<typename Callback>
    void feed(std::string_view chunk, Callback&& on_event) {
        buffer_.append(chunk.data(), chunk.size());

        while (read_offset_ < buffer_.size()) {
            auto nl = buffer_.find('\n', read_offset_);
            if (nl == std::string::npos) {
                break;
            }

            // 使用 string_view 避免每行 substr 拷贝
            auto line_sv = std::string_view(buffer_.data() + read_offset_, nl - read_offset_);
            auto view = (line_sv.size() > 0 && line_sv.back() == '\r')
                          ? line_sv.substr(0, line_sv.size() - 1)
                          : line_sv;

            if (view.empty()) {
                flush_event(on_event);
            } else if (view.size() > 6 && view.substr(0, 6) == "event:") {
                auto val = view.substr(6);
                if (!val.empty() && val[0] == ' ') val = val.substr(1);
                current_event_ = std::string(val.data(), val.size());
            } else if (view.size() > 5 && view.substr(0, 5) == "data:") {
                auto val = view.substr(5);
                if (!val.empty() && val[0] == ' ') val = val.substr(1);
                if (!current_data_.empty()) current_data_ += '\n';
                current_data_.append(val.data(), val.size());
            }

            read_offset_ = nl + 1;
        }

        if (read_offset_ > kCompactThreshold) {
            buffer_.erase(0, read_offset_);
            read_offset_ = 0;
        }
    }

    /// 输入结束后，刷新可能残留的事件
    template<typename Callback>
    void finish(Callback&& on_event) {
        if (read_offset_ < buffer_.size()) {
            auto line_sv = std::string_view(buffer_.data() + read_offset_, buffer_.size() - read_offset_);
            if (!line_sv.empty() && line_sv.back() == '\r') line_sv.remove_suffix(1);
            if (!line_sv.empty()) {
                if (line_sv.size() > 5 && line_sv.substr(0, 5) == "data:") {
                    auto val = line_sv.substr(5);
                    if (!val.empty() && val[0] == ' ') val = val.substr(1);
                    if (!current_data_.empty()) current_data_ += '\n';
                    current_data_.append(val.data(), val.size());
                }
            }
            buffer_.clear();
            read_offset_ = 0;
        }
        flush_event(on_event);
    }

private:
    static constexpr size_t kCompactThreshold = 64 * 1024;  // 累计 64KB 才压缩一次

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
    size_t read_offset_ = 0;
};

} // namespace ben_gear::llm
