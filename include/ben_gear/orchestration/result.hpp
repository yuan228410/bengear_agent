#pragma once

#include "ben_gear/llm/usage.hpp"
#include "ben_gear/orchestration/types.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::orchestration {

namespace execution_field {
inline constexpr std::string_view index = "index";
inline constexpr std::string_view total = "total";
inline constexpr std::string_view tool_name = "tool_name";
inline constexpr std::string_view tool_steps = "tool_steps";
inline constexpr std::string_view was_truncated = "was_truncated";
inline constexpr std::string_view was_summarized = "was_summarized";
inline constexpr std::string_view success = "success";
} // namespace execution_field

/// 结构化值：内部优先保持 String，JSON 只在边界生成。
struct ExecutionValue {
    container::String text;
    Metadata fields;

    bool empty() const noexcept {
        return text.empty() && fields.empty();
    }

    std::string_view text_view() const noexcept {
        return std::string_view(text.data(), text.size());
    }

    void set_text(std::string_view value) {
        text = container::String(value.data(), value.size());
    }

    void set_text(const char* value) {
        set_text(std::string_view(value ? value : ""));
    }

    void set_text(container::String value) {
        text = std::move(value);
    }

    void set_field(std::string_view key, std::string_view value) {
        fields[container::String(key.data(), key.size())] = container::String(value.data(), value.size());
    }

    void set_field(const char* key, const char* value) {
        set_field(std::string_view(key ? key : ""), std::string_view(value ? value : ""));
    }

    void set_field(std::string_view key, const char* value) {
        set_field(key, std::string_view(value ? value : ""));
    }

    void set_field(std::string_view key, const std::string& value) {
        set_field(key, std::string_view(value.data(), value.size()));
    }

    void set_field(std::string_view key, container::String value) {
        fields[container::String(key.data(), key.size())] = std::move(value);
    }

    void set_field(container::String key, container::String value) {
        fields[std::move(key)] = std::move(value);
    }

    void set_bool_field(std::string_view key, bool value) {
        set_field(key, value ? std::string_view("true") : std::string_view("false"));
    }

    std::string_view field_view(std::string_view key) const {
        const auto it = fields.find(container::String(key));
        if (it == fields.end()) {
            return {};
        }
        return std::string_view(it->second.data(), it->second.size());
    }

    bool field_equals(std::string_view key, std::string_view expected) const {
        return field_view(key) == expected;
    }

    bool field_bool(std::string_view key, bool default_value = false) const {
        const auto value = field_view(key);
        if (value == "true") {
            return true;
        }
        if (value == "false") {
            return false;
        }
        return default_value;
    }
};

/// 子执行摘要。避免 ExecutionResult 递归持有自身，降低拷贝和模板实例化成本。
struct ExecutionChildSummary {
    ExecutionId execution_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionStatus status = ExecutionStatus::pending;
    ExecutionValue output;
    container::String error;
};

/// 统一执行结果。可表达 sub-agent、workflow task、tool 等输出。
struct ExecutionResult {
    ExecutionId execution_id;
    ParentExecutionId parent_id;
    ExecutionKind kind = ExecutionKind::task;
    ExecutionStatus status = ExecutionStatus::pending;
    ExecutionValue output;
    container::String error;
    llm::TokenUsage usage;
    llm::RequestLatency latency;
    Metadata metrics;
    container::Vector<ExecutionChildSummary> children;
    TimePoint started_at{};
    TimePoint completed_at{};

    bool success() const noexcept { return status == ExecutionStatus::succeeded; }
    bool terminal() const noexcept { return is_terminal(status); }

    static ExecutionResult ok(ExecutionId id, ExecutionKind kind, ExecutionValue output = {}) {
        ExecutionResult result;
        result.execution_id = std::move(id);
        result.kind = kind;
        result.status = ExecutionStatus::succeeded;
        result.output = std::move(output);
        result.completed_at = Clock::now();
        return result;
    }

    static ExecutionResult failed(ExecutionId id, ExecutionKind kind, container::String error) {
        ExecutionResult result;
        result.execution_id = std::move(id);
        result.kind = kind;
        result.status = ExecutionStatus::failed;
        result.error = std::move(error);
        result.completed_at = Clock::now();
        return result;
    }
};

} // namespace ben_gear::orchestration
