#pragma once

#include "memory/types.hpp"
// fwd
namespace ben_gear::memory { class MemoryStore; }
namespace ben_gear::llm { class ConversationHistory; }

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

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
    sub_agent  = identity | directives | skills, // 子 Agent 最小上下文
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
    sub_agent,        // 子 Agent — 专注委派任务
};

// ─── ContextBuilder ──────────────────────────────────────────────────

/// 系统提示词组装器。
///
/// 用法：
///   ContextBuilder builder(store, skills_meta);
///   builder.set_mode(PromptMode::plan_reviewing);
///   builder.set_project_dir(path);
///   auto prompt = builder.build();
///
/// SubAgent 用法：
///   builder.set_section_mask(PromptSection::sub_agent);
///   builder.set_mode(PromptMode::sub_agent);
///   auto prompt = builder.build();
///
/// 内置缓存：记忆变更或配置变更时自动失效。
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   std::string skills_metadata = {});

    // ─── 区段控制 ──────────────────────────────────────────────

    /// 设置包含的区段（完全替换）
    void set_section_mask(PromptSection mask);
    PromptSection section_mask() const noexcept { return sections_; }

    // ─── 模式控制 ──────────────────────────────────────────────

    void set_mode(PromptMode mode);
    PromptMode mode() const noexcept { return mode_; }

    // ─── 内容注入 ──────────────────────────────────────────────

    /// 覆盖默认身份提示（为空则用 "You are BenGear, an AI agent."）
    void set_core_prompt(std::string prompt);

    /// 设置技能元数据（来自 SkillLoader）
    void set_skills_metadata(std::string skills_metadata);

    /// 设置项目根目录（用于 workspace 区段）
    void set_project_dir(const std::filesystem::path& dir);

    /// 是否注入 AGENTS.md 到 workspace 区段
    void set_inject_project_doc(bool enable) { inject_project_doc_ = enable; }

    // ─── 构建 ──────────────────────────────────────────────────

    /// 组装完整系统提示（带缓存）
    std::string build() const;

    /// 使用临时区段掩码和模式构建提示词，不修改 builder 状态，不访问缓存
    std::string build_with(PromptSection mask, PromptMode mode) const;

    /// 估算消息列表的 token 数
    static int64_t estimate_messages_tokens(const llm::ConversationHistory& history);

    /// 估算单段文本的 token 数
    static int64_t estimate_text_tokens(std::string_view text);

private:
    /// 无缓存、无锁的区段组装（build 和 build_with 共用）
    std::string build_unchecked(PromptSection sections, PromptMode mode) const;
    // 各区段生成函数
    std::string build_identity() const;
    std::string build_directives() const;
    std::string build_skills() const;
    std::string build_rules() const;
    std::string build_soul() const;
    std::string build_user() const;
    std::string build_memory() const;
    std::string build_workspace() const;

    std::string read_file_at_tier(const char* filename) const;
    std::string build_mode(PromptMode mode) const;
    void invalidate_cache();

    const MemoryStore& store_;

    PromptSection sections_ = PromptSection::standard;
    PromptMode mode_ = PromptMode::normal;

    std::string skills_metadata_;
    std::filesystem::path project_dir_;
    std::string core_prompt_;
    bool inject_project_doc_ = false;

    // 缓存：base 区段（identity..workspace，I/O 密集），mode 区段总是实时构建
    mutable std::string cached_base_;
    mutable bool base_valid_ = false;
    mutable std::mutex cache_mutex_;
};

}  // namespace ben_gear::memory
