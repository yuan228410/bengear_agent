#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 系统提示组装器 + token 估算
/// 组装顺序：SOUL → 核心提示 → RULES → 技能列表 → MEMORY → 工作空间信息 → 项目文档
class ContextBuilder {
public:
    ContextBuilder(const MemoryStore& memory_store,
                   const skill::SkillLoader& skill_loader)
        : memory_store_(memory_store), skill_loader_(skill_loader) {}

    /// 设置自定义核心提示（覆盖默认 "You are BenGear..." 提示）
    void set_core_prompt(const std::string& prompt) {
        core_prompt_ = prompt;
    }

    /// 组装完整系统提示
    /// exclude_character=true 时跳过 SOUL/core/RULES（用于 teammate）
    std::string build(bool exclude_character = false) const {
        std::string prompt;

        // 1. 身份定义（SOUL.md）
        if (!exclude_character) {
            auto soul = memory_store_.read_soul();
            if (!soul.empty()) {
                prompt += std::string(soul.data(), soul.size());
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
                prompt += std::string(rules.data(), rules.size());
                prompt += "\n\n---\n\n";
            }
        }

        // 4. 技能列表
        auto skills_meta = skill_loader_.get_skills_metadata();
        if (!skills_meta.empty()) {
            prompt += std::string(skills_meta.data(), skills_meta.size());
            prompt += "\nTo use a skill, call the get_skill tool with the skill name. "
                      "This loads detailed instructions into the conversation.\n\n";
        }

        // 5. 长期记忆（MEMORY.md）
        auto mem = memory_store_.read_memory();
        if (!mem.empty()) {
            auto mem_str = std::string(mem.data(), mem.size());
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
                prompt += mem_str;
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

    /// 设置项目目录
    void set_project_dir(const std::filesystem::path& dir) {
        project_dir_ = dir;
    }

    /// 估算消息列表的 token 数（CJK 感知字符启发式）
    /// CJK 字符 = 1 token，其他 = 0.25 token，每条消息 +4
    static int64_t estimate_messages_tokens(const llm::ConversationHistory& history) {
        int64_t total = 0;
        for (const auto& msg : history.messages()) {
            total += 4;  // 每条消息开销
            total += estimate_text_tokens(std::string_view(msg.content.data(), msg.content.size()));
            for (const auto& block : msg.blocks) {
                if (block.text) {
                    total += estimate_text_tokens(
                        std::string_view(block.text->data(), block.text->size()));
                }
                if (block.data) {
                    total += estimate_text_tokens(block.data->dump());
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
    /// 读取项目文档（AGENTS.md）
    std::string read_project_doc() const {
        if (project_dir_.empty()) return {};

        // 查找 AGENTS.md 或 CLAUDE.md
        for (const char* name : {"AGENTS.md", "CLAUDE.md"}) {
            auto path = project_dir_ / name;
            if (std::filesystem::exists(path)) {
                std::ifstream file(path, std::ios::binary);
                if (file) {
                    std::string content{
                        std::istreambuf_iterator<char>(file),
                        std::istreambuf_iterator<char>()};
                    if (!content.empty()) {
                        return "## Project Spec (" + std::string(name) + ")\n\n" + content;
                    }
                }
            }
        }
        return {};
    }

    const MemoryStore& memory_store_;
    const skill::SkillLoader& skill_loader_;
    std::filesystem::path project_dir_;
    std::string core_prompt_;
};

}  // namespace ben_gear::memory
