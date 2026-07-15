#pragma once


#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::llm {

namespace container = base::container;

enum class RunStatus {
    completed,
    interrupted,
    failed,
    cancelled,
};

enum class RunFinishReason {
    stop,
    tool_limit,
    invalid_input,
    user_cancelled,
    timeout,
    context_overflow,
    provider_error,
    transport_error,
    internal_error,
};

enum class RunSeverity {
    info,
    warning,
    error,
};

enum class RetryMode {
    none,
    retry_same,
    continue_run,
    compact_and_retry,
    adjust_settings,
    change_model,
    reauthenticate,
};

struct RetryAdvice {
    bool available = false;
    RetryMode mode = RetryMode::none;
    bool requires_user_confirmation = false;
    int after_seconds = 0;
    std::string reason;
};

struct RunOutcome {
    RunStatus status = RunStatus::completed;
    RunFinishReason reason = RunFinishReason::stop;
    RunSeverity severity = RunSeverity::info;
    std::string code = std::string("agent.stop");
    std::string source = std::string("agent");
    std::string message;
    std::string details_json;
    RetryAdvice retry;

    bool ok() const noexcept {
        return status == RunStatus::completed && reason == RunFinishReason::stop;
    }

    static RunOutcome success() {
        RunOutcome out;
        out.message = std::string("Completed");
        return out;
    }

    static RunOutcome invalid_input(std::string message) {
        RunOutcome out;
        out.status = RunStatus::failed;
        out.reason = RunFinishReason::invalid_input;
        out.severity = RunSeverity::error;
        out.code = std::string("agent.invalid_input");
        out.message = std::move(message);
        out.retry = RetryAdvice{true, RetryMode::adjust_settings, false, 0,
                                std::string("input can be corrected")};
        return out;
    }

    static RunOutcome provider_error(int status, std::string message) {
        RunOutcome out;
        out.status = RunStatus::failed;
        out.reason = RunFinishReason::provider_error;
        out.severity = RunSeverity::error;
        out.code = std::string("provider.error");
        out.source = std::string("provider");
        out.message = std::move(message);
        out.details_json = std::string("{\"http_status\":") + std::to_string(status) + "}";
        const bool retryable = status == 0 || status == 408 || status == 409 || status == 429 || status >= 500;
        out.retry = RetryAdvice{retryable,
                                retryable ? RetryMode::retry_same : RetryMode::none,
                                retryable,
                                status == 429 ? 10 : 0,
                                retryable ? std::string("provider error may be transient")
                                          : std::string("provider error is not retryable")};
        return out;
    }

    static RunOutcome context_overflow(std::string message) {
        RunOutcome out;
        out.status = RunStatus::failed;
        out.reason = RunFinishReason::context_overflow;
        out.severity = RunSeverity::error;
        out.code = std::string("agent.context_overflow");
        out.message = std::move(message);
        out.retry = RetryAdvice{true, RetryMode::compact_and_retry, true, 0,
                                std::string("context can be compacted before retry")};
        return out;
    }

    static RunOutcome tool_limit(int max_steps,
                                 int steps_used = -1,
                                 int max_tool_calls = 0,
                                 int tool_calls_used = 0,
                                 int max_tool_calls_per_step = 0,
                                 int tool_calls_in_step = 0,
                                 std::string message = std::string("Tool call limit reached")) {
        RunOutcome out;
        out.status = RunStatus::interrupted;
        out.reason = RunFinishReason::tool_limit;
        out.severity = RunSeverity::warning;
        out.code = std::string("agent.tool_limit");
        out.message = std::move(message);
        const int used_steps = steps_used >= 0 ? steps_used : max_steps;
        out.details_json = std::string("{\"max_steps\":") + std::to_string(max_steps)
            + ",\"steps_used\":" + std::to_string(used_steps)
            + ",\"max_tool_calls\":" + std::to_string(max_tool_calls)
            + ",\"tool_calls_used\":" + std::to_string(tool_calls_used)
            + ",\"max_tool_calls_per_step\":" + std::to_string(max_tool_calls_per_step)
            + ",\"tool_calls_in_step\":" + std::to_string(tool_calls_in_step) + "}";
        out.retry = RetryAdvice{true, RetryMode::continue_run, true, 0,
                                std::string("run can continue with more tool budget")};
        return out;
    }

    static RunOutcome cancelled(std::string message = std::string("Cancelled")) {
        RunOutcome out;
        out.status = RunStatus::cancelled;
        out.reason = RunFinishReason::user_cancelled;
        out.severity = RunSeverity::info;
        out.code = std::string("agent.cancelled");
        out.message = std::move(message);
        out.retry = RetryAdvice{true, RetryMode::retry_same, true, 0,
                                std::string("user may restart the request")};
        return out;
    }

    static RunOutcome timeout(std::string message = std::string("Timed out")) {
        RunOutcome out;
        out.status = RunStatus::interrupted;
        out.reason = RunFinishReason::timeout;
        out.severity = RunSeverity::warning;
        out.code = std::string("agent.timeout");
        out.message = std::move(message);
        out.retry = RetryAdvice{true, RetryMode::retry_same, true, 0,
                                std::string("timeout may be transient")};
        return out;
    }

    static RunOutcome internal_error(std::string message) {
        RunOutcome out;
        out.status = RunStatus::failed;
        out.reason = RunFinishReason::internal_error;
        out.severity = RunSeverity::error;
        out.code = std::string("agent.internal_error");
        out.message = std::move(message);
        out.retry = RetryAdvice{true, RetryMode::retry_same, true, 0,
                                std::string("internal error may be transient")};
        return out;
    }
};

inline const char* to_string(RunStatus status) noexcept {
    switch (status) {
    case RunStatus::completed: return "completed";
    case RunStatus::interrupted: return "interrupted";
    case RunStatus::failed: return "failed";
    case RunStatus::cancelled: return "cancelled";
    }
    return "failed";
}

inline const char* to_string(RunFinishReason reason) noexcept {
    switch (reason) {
    case RunFinishReason::stop: return "stop";
    case RunFinishReason::tool_limit: return "tool_limit";
    case RunFinishReason::invalid_input: return "invalid_input";
    case RunFinishReason::user_cancelled: return "user_cancelled";
    case RunFinishReason::timeout: return "timeout";
    case RunFinishReason::context_overflow: return "context_overflow";
    case RunFinishReason::provider_error: return "provider_error";
    case RunFinishReason::transport_error: return "transport_error";
    case RunFinishReason::internal_error: return "internal_error";
    }
    return "internal_error";
}

inline const char* to_string(RunSeverity severity) noexcept {
    switch (severity) {
    case RunSeverity::info: return "info";
    case RunSeverity::warning: return "warning";
    case RunSeverity::error: return "error";
    }
    return "error";
}

inline const char* to_string(RetryMode mode) noexcept {
    switch (mode) {
    case RetryMode::none: return "none";
    case RetryMode::retry_same: return "retry_same";
    case RetryMode::continue_run: return "continue_run";
    case RetryMode::compact_and_retry: return "compact_and_retry";
    case RetryMode::adjust_settings: return "adjust_settings";
    case RetryMode::change_model: return "change_model";
    case RetryMode::reauthenticate: return "reauthenticate";
    }
    return "none";
}

inline std::string escape_json_string(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 16);
    for (char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out += ' ';
            } else {
                out += ch;
            }
            break;
        }
    }
    return out;
}

inline std::string to_json(const RetryAdvice& retry) {
    std::string json = "{";
    json += "\"available\":";
    json += retry.available ? "true" : "false";
    json += ",\"mode\":\"";
    json += to_string(retry.mode);
    json += "\",\"requires_user_confirmation\":";
    json += retry.requires_user_confirmation ? "true" : "false";
    json += ",\"after_seconds\":";
    json += std::to_string(retry.after_seconds);
    if (!retry.reason.empty()) {
        json += ",\"reason\":\"";
        json += escape_json_string(std::string_view(retry.reason.data(), retry.reason.size()));
        json += "\"";
    }
    json += "}";
    return json;
}

inline std::string to_json(const RunOutcome& outcome) {
    std::string json = "{";
    json += "\"status\":\"";
    json += to_string(outcome.status);
    json += "\",\"reason\":\"";
    json += to_string(outcome.reason);
    json += "\",\"severity\":\"";
    json += to_string(outcome.severity);
    json += "\",\"code\":\"";
    json += escape_json_string(std::string_view(outcome.code.data(), outcome.code.size()));
    json += "\",\"source\":\"";
    json += escape_json_string(std::string_view(outcome.source.data(), outcome.source.size()));
    json += "\",\"message\":\"";
    json += escape_json_string(std::string_view(outcome.message.data(), outcome.message.size()));
    json += "\",\"retry\":";
    json += to_json(outcome.retry);
    if (!outcome.details_json.empty()) {
        json += ",\"details\":";
        json += outcome.details_json;
    }
    json += "}";
    return json;
}

} // namespace ben_gear::llm

namespace ben_gear {
using RunOutcome = llm::RunOutcome;
using RetryAdvice = llm::RetryAdvice;
using RunStatus = llm::RunStatus;
using RunFinishReason = llm::RunFinishReason;
using RunSeverity = llm::RunSeverity;
using RetryMode = llm::RetryMode;
}
