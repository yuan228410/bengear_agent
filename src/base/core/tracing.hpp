#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ben_gear::base {

/// Span 类型
enum class SpanKind {
    Internal,   // 内部操作
    LLM,        // LLM 调用
    Tool,       // 工具执行
    SubAgent,   // 子 Agent 任务
    Workflow,   // 工作流
};

/// Span 上下文（用于嵌套追踪）
struct SpanContext {
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
};

/// Span 快照（用于导出/序列化）
struct SpanSnapshot {
    std::string name;
    SpanKind kind;
    SpanContext context;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::microseconds duration;
    std::unordered_map<std::string, std::string> attributes;
    bool error = false;
    std::string error_message;
};

/// 追踪器接口 — 基于 Span 的链路追踪
///
/// 注册到 ServiceRegistry，支持 OpenTelemetry 等后端。
class ITracer {
public:
    virtual ~ITracer() = default;

    /// 开始一个 Span
    /// @return Span 引用（用于结束 span 时传回）
    virtual uint64_t start_span(std::string_view name,
                                 SpanKind kind,
                                 std::unordered_map<std::string, std::string> attributes = {}) = 0;

    /// 结束 Span
    virtual void end_span(uint64_t span_id,
                          bool error = false,
                          std::string_view error_message = {}) = 0;

    /// 设置 Span 属性
    virtual void set_attribute(uint64_t span_id,
                                std::string_view key,
                                std::string_view value) = 0;

    /// 获取当前 Span ID（用于嵌套）
    virtual uint64_t current_span_id() const = 0;
};

/// 空实现 — 编译期零开销
class NoopTracer : public ITracer {
public:
    uint64_t start_span(std::string_view, SpanKind,
                         std::unordered_map<std::string, std::string>) override {
        return next_id_++;
    }
    void end_span(uint64_t, bool, std::string_view) override {}
    void set_attribute(uint64_t, std::string_view, std::string_view) override {}
    uint64_t current_span_id() const override { return 0; }

private:
    uint64_t next_id_ = 1;
};

/// Span RAII 辅助 — 构造时开始，析构时结束
class ScopedSpan {
public:
    ScopedSpan(ITracer* tracer,
               std::string_view name,
               SpanKind kind,
               std::unordered_map<std::string, std::string> attrs = {})
        : tracer_(tracer) {
        if (tracer_) span_id_ = tracer_->start_span(name, kind, std::move(attrs));
    }

    ~ScopedSpan() {
        if (tracer_ && span_id_ > 0) {
            tracer_->end_span(span_id_, error_, error_message_);
        }
    }

    void set_error(std::string_view msg) {
        error_ = true;
        error_message_ = msg;
    }

    void set_attribute(std::string_view key, std::string_view value) {
        if (tracer_ && span_id_ > 0) {
            tracer_->set_attribute(span_id_, key, value);
        }
    }

    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

private:
    ITracer* tracer_ = nullptr;
    uint64_t span_id_ = 0;
    bool error_ = false;
    std::string error_message_;
};

} // namespace ben_gear::base
