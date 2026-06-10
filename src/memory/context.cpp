#include "ben_gear/memory/context.hpp"

#include <fstream>

namespace ben_gear::memory {

void ContextBuilder::set_core_prompt(const std::string& prompt) {
    core_prompt_ = prompt;
    cache_valid_ = false;
}

void ContextBuilder::set_project_dir(const std::filesystem::path& dir) {
    project_dir_ = dir;
    cache_valid_ = false;
}

std::string ContextBuilder::build(bool exclude_character) const {
    if (cache_valid_ && cached_exclude_character_ == exclude_character &&
        !memory_store_.is_dirty()) {
        return cached_prompt_;
    }

    std::string prompt = build_inner(exclude_character);

    cached_prompt_ = prompt;
    cached_exclude_character_ = exclude_character;
    cache_valid_ = true;
    memory_store_.clear_dirty();

    return prompt;
}

int64_t ContextBuilder::estimate_messages_tokens(
    const workspace::ConversationHistory& history) {
    int64_t total = 0;
    for (const auto& msg : history.messages()) {
        total += 4;
        auto text = msg.get_all_text();
        total += estimate_text_tokens(
            std::string_view(text.data(), text.size()));

        for (const auto& block : msg.content()) {
            if (block.is_tool_use()) {
                auto call = block.tool_use();
                total += estimate_text_tokens(
                    std::string_view(call.name.data(), call.name.size()));
                total += estimate_text_tokens(call.arguments.dump());
            }
        }
    }
    return total;
}

int64_t ContextBuilder::estimate_text_tokens(std::string_view text) {
    int64_t tokens = 0;
    int ascii_count = 0;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0xF0) {
            tokens += 1;
            i += 4;
        } else if (c >= 0xE0) {
            tokens += 1;
            i += 3;
        } else if (c >= 0xC0) {
            tokens += 1;
            i += 2;
        } else {
            ascii_count++;
            i += 1;
            if (ascii_count == 4) {
                tokens += 1;
                ascii_count = 0;
            }
        }
    }
    if (ascii_count > 0) tokens += 1;
    return tokens;
}

std::string ContextBuilder::build_inner(bool exclude_character) const {
    size_t estimated = 1024;
    estimated += core_prompt_.size();
    estimated += 4096;
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

    // 1.5 用户偏好（USER.md）
    if (!exclude_character) {
        auto user_prefs = read_file_at_tier("USER.md");
        if (!user_prefs.empty()) {
            prompt += "## User Preferences\n\n";
            prompt.append(user_prefs.data(), user_prefs.size());
            prompt += "\n\n---\n\n";
        }
    }

    // 2. 核心提示
    if (!exclude_character) {
        if (!core_prompt_.empty()) {
            prompt += core_prompt_;
            prompt += "\n\n";
        } else {
            prompt +=
                "You are BenGear, a concise cross-platform coding agent. "
                "Prefer direct, actionable answers and avoid unnecessary "
                "dependencies.\n\n";
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
        prompt +=
            "\nTo use a skill, call the get_skill tool with the skill "
            "name. "
            "This loads detailed instructions into the conversation.\n\n";
    }

    // 5. 长期记忆（MEMORY.md）
    auto mem = memory_store_.read_memory();
    if (!mem.empty()) {
        auto mem_str = std::string_view(mem.data(), mem.size());
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

container::String ContextBuilder::read_file_at_tier(
    const char* filename) const {
    for (auto tier :
         {base::Tier::global, base::Tier::user, base::Tier::workspace}) {
        auto path =
            memory_store_.tier_paths().dir(tier) / "memory" / filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) continue;
        auto size = file.tellg();
        if (size <= 0) continue;
        file.seekg(0, std::ios::beg);
        std::vector<char> buf(static_cast<size_t>(size));
        file.read(buf.data(), static_cast<std::streamsize>(size));
        if (!file) continue;
        return container::String(buf.data(), static_cast<size_t>(size));
    }
    return {};
}

std::string ContextBuilder::read_project_doc() const {
    if (project_dir_.empty()) return {};

    for (const char* name : {"AGENTS.md", "CLAUDE.md"}) {
        auto path = project_dir_ / name;
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
            return "## Project Spec (" + std::string(name) + ")\n\n" +
                   content;
        }
    }
    return {};
}

}  // namespace ben_gear::memory
