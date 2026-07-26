#pragma once

#include <cstdint>

namespace ben_gear::memory {

// ─── 提示词区段掩码 ──────────────────────────────────────────────────

/// 系统提示词由多个区段组成，调用方按位控制包含哪些区段。
/// 区段按固定顺序组装：identity → directives → skills →
///   rules → soul → user → memory → workspace → mode
enum class PromptSection : uint16_t {
    identity   = 1 << 0,  // "You are BenGear..." + 自定义 core_prompt
    directives = 1 << 1,  // 行为效率指令
    skills     = 1 << 2,  // 技能元数据
    rules      = 1 << 3,  // RULES.md
    soul       = 1 << 4,  // SOUL.md
    user       = 1 << 5,  // USER.md
    memory     = 1 << 6,  // MEMORY.md（长期记忆）
    workspace  = 1 << 7,  // 工作空间路径 + AGENTS.md

    // 预设组合
    character  = rules | soul | user,           // 身份定义区段
    standard   = identity | directives | skills | character | memory | workspace,
};

inline constexpr PromptSection operator|(PromptSection a, PromptSection b) {
    return static_cast<PromptSection>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
inline constexpr bool operator&(PromptSection a, PromptSection b) {
    return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}

// ─── 运行模式 ────────────────────────────────────────────────────────

/// 控制最后注入的模式指令区段内容
enum class PromptMode : uint8_t {
    normal,           // 无额外指令
    plan_reviewing,   // 计划审核中 — 只规划不实现
    plan_executing,   // 计划执行中 — 按步骤执行
};

}  // namespace ben_gear::memory
