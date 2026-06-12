#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/workspace/conversation_history.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 系统提示组装器 + token 估算
/// 内置缓存：记忆变更时自动失效，避免重复磁盘 I/O + 合并计算
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   const skill::SkillLoader& skill_loader)
        : memory_store_(memory_store), skill_loader_(skill_loader) {}

    /// 设置自定义核心提示
    void set_core_prompt(const std::string& prompt);

    /// 设置项目目录
    void set_project_dir(const std::filesystem::path& dir);

    /// 组装完整系统提示（带缓存）
    std::string build(bool exclude_character = false) const;

    /// 估算消息列表的 token 数（CJK 感知字符启发式）
    static int64_t estimate_messages_tokens(
        const workspace::ConversationHistory& history);

    /// 估算单段文本的 token 数
    static int64_t estimate_text_tokens(std::string_view text);

private:
    std::string build_inner(bool exclude_character) const;
    container::String read_file_at_tier(const char* filename) const;
    std::string read_project_doc() const;

    const MemoryStore& memory_store_;
    const skill::SkillLoader& skill_loader_;
    std::filesystem::path project_dir_;
    std::string core_prompt_;

    mutable std::string cached_prompt_;
    mutable bool cache_valid_ = false;
    mutable bool cached_exclude_character_ = false;
};

}  // namespace ben_gear::memory
