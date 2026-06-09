#pragma once

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
/// - 参数全部 string_view，零 DTO 耦合
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

    // ---- 计划模式 ----

    /// 显示计划步骤列表（每行一步，格式: "1. description"）
    virtual void on_plan_steps(std::string_view steps_text) = 0;
    /// 步骤开始执行（如: "▶ Step 2/5: Fix the bug"）
    virtual void on_step_started(int step_index, int total, std::string_view description) = 0;
    /// 步骤完成
    virtual void on_step_completed(int step_index, std::string_view result) = 0;
    /// 步骤跳过
    virtual void on_step_skipped(int step_index, std::string_view description) = 0;
    /// 计划全部完成
    virtual void on_plan_finished() = 0;
    /// 计划模式提示（如: 工具调用被拦截时）
    virtual void on_plan_message(std::string_view message) = 0;
};

/// 创建终端富文本 Renderer
/// 参数均为前置声明类型，实现在 renderer.cpp 中
std::unique_ptr<Renderer> create_terminal_renderer(const Theme& theme,
                                                    const TerminalCapabilities& cap,
                                                    const DisplayConfig& config);

/// 创建静默 Renderer（不输出任何内容）
std::unique_ptr<Renderer> create_silent_renderer();

}  // namespace ben_gear::cli
