#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 系统提示组装器 + token 估算
/// 组装顺序：SOUL → 核心提示 → RULES → 技能列表 → MEMORY → 工作空间信息 → 项目文档
/// 内置缓存：记忆变更时自动失效，避免重复磁盘 I/O + 合并计算
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   const skill::SkillLoader& skill_loader)
        : memory_store_(memory_store), skill_loader_(skill_loader) {}

    /// 设置自定义核心提示（覆盖默认 "You are BenGear..." 提示）
    void set_core_prompt(const std::string& prompt) {
        core_prompt_ = prompt;
        cache_valid_ = false;  // 核心提示变更，缓存失效
    }

    /// 设置项目目录
    void set_project_dir(const std::filesystem::path& dir) {
        project_dir_ = dir;
        cache_valid_ = false;  // 工作空间变更，缓存失效
    }

    /// 组装完整系统提示（带缓存，记忆变更时自动重建）
    /// exclude_character=true 时跳过 SOUL/core/RULES（用于 teammate）
    std::string build(bool exclude_character = false) const {
        // 检查缓存是否有效：缓存有效 且 记忆未变更 且 参数一致
        if (cache_valid_ && cached_exclude_character_ == exclude_character
            && !memory_store_.is_dirty()) {
            return cached_prompt_;
        }

        // 重建
        std::string prompt = build_inner(exclude_character);

        // 更新缓存
        cached_prompt_ = prompt;
        cached_exclude_character_ = exclude_character;
        cache_valid_ = true;
        memory_store_.clear_dirty();

        return prompt;
    }

    /// 估算消息列表的 token 数（CJK 感知字符启发式）
    /// CJK 字符 = 1 token，其他 = 0.25 token，每条消息 +4
    static int64_t estimate_messages_tokens(const workspace::ConversationHistory& history) {
        int64_t total = 0;
        for (const auto& msg : history.messages()) {
            total += 4;  // 每条消息开销
            // 获取消息的所有文本内容
            auto text = msg.get_all_text();
            total += estimate_text_tokens(std::string_view(text.data(), text.size()));
            
            // 处理工具调用
            for (const auto& block : msg.content()) {
                if (block.is_tool_use()) {
                    auto call = block.tool_use();
                    total += estimate_text_tokens(std::string_view(call.name.data(), call.name.size()));
                    total += estimate_text_tokens(call.arguments.dump());
                }
            }
        }
        return total;
    }

    /// 估算单段文本的 token 数
    static int64_t estimate_text_tokens(std::string_view text) {
        int64_t tokens = 0;
        int ascii_count = 0;
        for (size_t i = 0; i < text.size(); ) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            if (c >= 0xF0) {
                // 4-byte UTF-8 (emoji 等)
                tokens += 1;
                i += 4;
            } else if (c >= 0xE0) {
                // 3-byte UTF-8 (CJK 等)
                tokens += 1;
                i += 3;
            } else if (c >= 0xC0) {
                // 2-byte UTF-8 (带重音拉丁、西里尔等)
                tokens += 1;
                i += 2;
            } else {
                // ASCII / 单字节
                ascii_count++;
                i += 1;
                // 每 4 个 ASCII 字符算 1 token
                if (ascii_count == 4) {
                    tokens += 1;
                    ascii_count = 0;
                }
            }
        }
        // 剩余不足 4 个的 ASCII 字符也算 1 token
        if (ascii_count > 0) tokens += 1;
        return tokens;
    }

private:
    /// 实际组装逻辑
    std::string build_inner(bool exclude_character) const {
        // 预估大小，减少 realloc
        size_t estimated = 1024;
        estimated += core_prompt_.size();
        estimated += 4096;  // 记忆内容预估
        std::string prompt;
        prompt.reserve(estimated);

        // 1. 身份定义（SOUL.md）
        if (!exclude_character) {
            auto soul = memory_store_.read_soul();
            if (!soul.empty()) {
                prompt.append(soul.data(), soul.size());
                prompt += "\n\n---\n\n";
            }
        }

        // 2. 核心提示
        if (!exclude_character) {
            if (!core_prompt_.empty()) {
                prompt += core_prompt_;
                prompt += "\n\n";
            } else {
                prompt += "You are BenGear, a concise cross-platform coding agent. "
                          "Prefer direct, actionable answers and avoid unnecessary dependencies.\n\n";
            }
        }

        // 3. 行为规范（RULES.md）
        if (!exclude_character) {
            auto rules = memory_store_.read_rules();
            if (!rules.empty()) {
                prompt.append(rules.data(), rules.size());
                prompt += "\n\n---\n\n";
            }
        }

        // 4. 技能列表
        auto skills_meta = skill_loader_.get_skills_metadata();
        if (!skills_meta.empty()) {
            prompt.append(skills_meta.data(), skills_meta.size());
            prompt += "\nTo use a skill, call the get_skill tool with the skill name. "
                      "This loads detailed instructions into the conversation.\n\n";
        }

        // 5. 长期记忆（MEMORY.md）
        auto mem = memory_store_.read_memory();
        if (!mem.empty()) {
            auto mem_str = std::string_view(mem.data(), mem.size());
            // 跳过空记忆（只有标题无内容）
            bool has_content = false;
            for (char c : mem_str) {
                if (c != '\n' && c != '\r' && c != ' ' && c != '#') {
                    has_content = true;
                    break;
                }
            }
            if (has_content) {
                prompt += "## Long-term Memory\n\n";
                prompt.append(mem.data(), mem.size());
                prompt += "\n\n";
            }
        }

        // 6. 工作空间信息
        if (!project_dir_.empty()) {
            prompt += "## Current Workspace\n\n";
            prompt += "Project path: ";
            prompt += project_dir_.string();
            prompt += "\n";
        }

        // 7. 项目文档（AGENTS.md）
        auto doc = read_project_doc();
        if (!doc.empty()) {
            prompt += "\n\n---\n\n";
            prompt += doc;
        }

        return prompt;
    }

    /// 读取项目文档（AGENTS.md / CLAUDE.md）
    std::string read_project_doc() const {
        if (project_dir_.empty()) return {};

        for (const char* name : {"AGENTS.md", "CLAUDE.md"}) {
            auto path = project_dir_ / name;
            // 直接 ifstream，省去 exists() 系统调用
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) continue;
            auto size = file.tellg();
            if (size <= 0) continue;
            file.seekg(0, std::ios::beg);
            std::vector<char> buf(static_cast<size_t>(size));
            file.read(buf.data(), static_cast<std::streamsize>(size));
            if (!file) continue;
            std::string content(buf.data(), static_cast<size_t>(size));
            if (!content.empty()) {
                return "## Project Spec (" + std::string(name) + ")\n\n" + content;
            }
        }
        return {};
    }

    const MemoryStore& memory_store_;
    const skill::SkillLoader& skill_loader_;
    std::filesystem::path project_dir_;
    std::string core_prompt_;

    /// 缓存
    mutable std::string cached_prompt_;
    mutable bool cache_valid_ = false;
    mutable bool cached_exclude_character_ = false;
};

}  // namespace ben_gear::memory
