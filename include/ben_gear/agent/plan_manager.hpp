#pragma once

namespace ben_gear::agent {

/// 计划模式管理器 — 纯两态状态机
///
/// 模式状态机：
/// [Normal] ────/plan────▶ [Planning]
///     ▲                      │
///     └──── /plan off ───────┘
///
/// Planning 模式：硬约束 read_only 工具，LLM 只能探索
/// Normal 模式：无约束，LLM 可执行所有工具
///
/// 设计原则：
/// 1. 零 I/O：不读写文件、不访问终端、不输出任何字符
/// 2. 零 UI：不包含 ANSI 转义码、emoji、格式化字符串
/// 3. 结构化输出：状态查询返回枚举，格式化由 UI 层负责
class PlanManager {
public:
    enum class Mode { normal, planning };

    // ---- 模式切换 ----

    void enter_plan_mode() {
        mode_ = Mode::planning;
        plan_prompt_injected_ = false;
    }

    void exit_plan_mode() {
        mode_ = Mode::normal;
        plan_prompt_injected_ = false;
    }

    // ---- 状态查询 ----

    Mode mode() const noexcept { return mode_; }
    bool in_plan_mode() const noexcept { return mode_ == Mode::planning; }
    bool is_active() const noexcept { return mode_ != Mode::normal; }

    // ---- 提示注入状态 ----

    /// 标记计划模式系统提示已注入（避免重复注入）
    void mark_prompt_injected() { plan_prompt_injected_ = true; }

    /// 计划模式系统提示是否已注入
    bool is_prompt_injected() const noexcept { return plan_prompt_injected_; }

private:
    Mode mode_ = Mode::normal;
    bool plan_prompt_injected_ = false;
};

} // namespace ben_gear::agent

namespace ben_gear {
using PlanManager = agent::PlanManager;
}
