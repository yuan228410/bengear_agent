#pragma once

#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::cli {

struct Theme;
struct TerminalCapabilities;
struct DisplayConfig;

/// Renderer 接口 — 展示层抽象
///
/// 职责：将 Agent 事件渲染为用户可见的输出
/// 实现：TerminalRenderer（终端富文本）、SilentRenderer（静默）
///
/// 设计原则：
/// - 纯接口，零依赖，零实现细节暴露
/// - 参数全部 string_view / 枚举，零 DTO 耦合
/// - 工厂函数创建，调用者不接触具体类型
class Renderer {
public:
    virtual ~Renderer() = default;

    /// LLM 开始回复
    virtual void on_response_start() = 0;
    /// LLM 回复结束
    virtual void on_response_end() = 0;

    /// 流式助手文本 token
    virtual void on_assistant_text(std::string_view token) = 0;
    /// 思考过程 token
    virtual void on_thinking(std::string_view token) = 0;
    /// 错误消息
    virtual void on_error(std::string_view message) = 0;
    /// 系统提示
    virtual void on_system(std::string_view message) = 0;

    /// 工具调用开始
    virtual void on_tool_call(std::string_view id, std::string_view name, std::string_view args_json) = 0;
    /// 工具执行完成
    virtual void on_tool_result(std::string_view id, std::string_view name, bool success, std::string_view output, size_t output_size) = 0;

    // ---- 模式变更 ----

    /// 计划模式变更：normal ↔ planning
    /// TerminalRenderer 渲染为 🔒/🔓 提示，SilentRenderer 忽略
    virtual void on_mode_changed(PlanManager::Mode mode) = 0;

    // ---- 工具拦截 ----

    /// plan 模式下非 read_only 工具被拦截
    /// TerminalRenderer 渲染为 └ ✗ tool_name — reason
    virtual void on_tool_blocked(std::string_view tool_name, std::string_view reason) = 0;

    // ---- 响应统计 ----

    /// LLM 响应完成后的 token 用量和延迟统计
    virtual void on_usage_stats(int prompt_tokens, int completion_tokens,
                                double total_seconds, double ttfb_seconds,
                                bool has_ttfb,
                                std::string_view model_name,
                                int64_t context_length) = 0;
};

/// 创建终端富文本 Renderer
std::unique_ptr<Renderer> create_terminal_renderer(const Theme& theme,
                                                    const TerminalCapabilities& cap,
                                                    const DisplayConfig& config);

/// 创建静默 Renderer（不输出任何内容）
std::unique_ptr<Renderer> create_silent_renderer();

} // namespace ben_gear::cli
