#include "memory/context.hpp"
#include <filesystem>
#include <sstream>
#include <mutex>
#include "llm/conversation_history.hpp"
#include "memory/store.hpp"
#include "platform/os.hpp"

#include <fstream>

namespace ben_gear::memory {

namespace {

/// 检查字符串是否只有空白和 Markdown 标题标记
bool has_visible_content(std::string_view text) {
    for (char c : text) {
        if (c != '\n' && c != '\r' && c != ' ' && c != '#') return true;
    }
    return false;
}

/// 读取项目根目录下的文档文件（AGENTS.md, CLAUDE.md 等）
std::string find_project_doc(const std::filesystem::path& project_dir) {
    static const char* candidates[] = {"AGENTS.md", "CLAUDE.md", "README.md"};
    for (auto name : candidates) {
        auto path = project_dir / name;
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec)) {
            std::ifstream file(path, std::ios::binary);
            if (file) {
                std::ostringstream oss;
                oss << file.rdbuf();
                std::string content = oss.str();
                if (!content.empty()) return content;
            }
        }
    }
    return {};
}

}  // namespace

// ─── ContextBuilder ────────────────────────────────────────────────────

ContextBuilder::ContextBuilder(const MemoryStore& store, std::string skills_meta)
    : store_(store), skills_metadata_(std::move(skills_meta)) {}

void ContextBuilder::set_section_mask(PromptSection mask) {
    sections_ = mask;
    invalidate_cache();
}

void ContextBuilder::set_mode(PromptMode mode) {
    mode_ = mode;
    // mode 切换不影响 base 缓存（build_mode 无 I/O，总是实时构建）
}

void ContextBuilder::set_core_prompt(std::string prompt) {
    core_prompt_ = std::move(prompt);
    invalidate_cache();
}

void ContextBuilder::set_skills_metadata(std::string skills_metadata) {
    skills_metadata_ = std::move(skills_metadata);
    invalidate_cache();
}

void ContextBuilder::set_project_dir(const std::filesystem::path& dir) {
    project_dir_ = dir;
    invalidate_cache();
}

void ContextBuilder::invalidate_cache() {
    std::lock_guard lock(cache_mutex_);
    base_valid_ = false;
}

std::string ContextBuilder::build_unchecked(PromptSection sections, PromptMode mode) const {
    std::string prompt;
    prompt.reserve(4096);

    auto has = [&](PromptSection s) { return sections & s; };

    if (has(PromptSection::identity))    prompt += build_identity();
    if (has(PromptSection::directives))  prompt += build_directives();
    if (has(PromptSection::skills))      prompt += build_skills();
    if (has(PromptSection::rules))       prompt += build_rules();
    if (has(PromptSection::soul))        prompt += build_soul();
    if (has(PromptSection::user))        prompt += build_user();
    if (has(PromptSection::memory))      prompt += build_memory();
    if (has(PromptSection::workspace))   prompt += build_workspace();
    prompt += build_mode(mode);

    return prompt;
}

std::string ContextBuilder::build() const {
    std::lock_guard lock(cache_mutex_);
    if (!base_valid_ || store_.is_dirty()) {
        cached_base_.clear();
        cached_base_.reserve(4096);
        auto has = [&](PromptSection s) { return sections_ & s; };
        if (has(PromptSection::identity))    cached_base_ += build_identity();
        if (has(PromptSection::directives))  cached_base_ += build_directives();
        if (has(PromptSection::skills))      cached_base_ += build_skills();
        if (has(PromptSection::rules))       cached_base_ += build_rules();
        if (has(PromptSection::soul))        cached_base_ += build_soul();
        if (has(PromptSection::user))        cached_base_ += build_user();
        if (has(PromptSection::memory))      cached_base_ += build_memory();
        if (has(PromptSection::workspace))   cached_base_ += build_workspace();
        base_valid_ = true;
        store_.clear_dirty();
    }
    return cached_base_ + build_mode(mode_);
}

std::string ContextBuilder::build_with(PromptSection mask, PromptMode mode) const {
    return build_unchecked(mask, mode);
}

// ─── 各区段生成 ────────────────────────────────────────────────────────

std::string ContextBuilder::build_identity() const {
    if (!core_prompt_.empty()) {
        return core_prompt_ + "\n\n";
    }
    return "You are BenGear, an AI agent.\n\n";
}

std::string ContextBuilder::build_directives() const {
    // 环境信息（单行，低 token 开销）
    std::string env;
#if BEN_GEAR_PLATFORM_WINDOWS
    env = "win32";
#elif BEN_GEAR_PLATFORM_MACOS
    env = "macOS";
#elif BEN_GEAR_PLATFORM_LINUX
    env = "linux";
#else
    env = "unknown";
#endif
    auto shell_env = ben_gear::base::platform::os::getenv_optional("SHELL");
    if (shell_env.has_value() && !shell_env->empty()) {
        auto pos = shell_env->find_last_of("/\\");
        env += " shell=";
        env.append((pos == std::string::npos) ? *shell_env : shell_env->substr(pos + 1));
    }
    env += " cols=" + std::to_string(ben_gear::base::platform::compat::terminal_width());

    return
        "Work efficiently: inspect high-signal targets first, avoid redundant reads, "
        "stop when evidence is sufficient.\n\n"
        "For multi-step tasks, create one TODO per step using update_todo before starting work, "
        "so the user can track progress. Skip for simple one-answer questions.\n\n"
        "Keep your context clean. Offload noisy, lengthy, or narrow sub-tasks to "
        "sub-agents via delegate_task (single) or delegate_tasks (parallel).\n"
        "Good candidates: scraping web pages, searching files/directories, "
        "batch commands, exploratory grep, parsing logs, formatting/translating.\n"
        "IMPORTANT: When delegating, ALWAYS include \"return a concise summary\" "
        "in the sub-agent's prompt. The sub-agent has no context of its own — "
        "everything it needs must be in your task prompt. "
        "You focus on decision making and synthesis.\n\n"

        "For multi-agent collaboration, create teams via create_team or team_create. "
        "Teams have long-lived agents with independent memory and roles. "
        "Use team_assign to give a Lead a high-level task; the Lead manages its team. "
        "Use team_send to talk to any member directly. "
        "Use team_status or team_list to check progress.\n\n"

        "Environment: " + env + "\n\n";
}

std::string ContextBuilder::build_skills() const {
    if (skills_metadata_.empty()) return {};
    std::string s;
    s.reserve(skills_metadata_.size() + 128);
    s.append(skills_metadata_.data(), skills_metadata_.size());
    s += "\nTo use a skill, call the get_skill tool with the skill name. "
         "This loads detailed instructions into the conversation.\n\n";
    return s;
}

std::string ContextBuilder::build_rules() const {
    auto rules = store_.read_rules();
    if (rules.empty()) return {};
    std::string s;
    s.reserve(rules.size() + 16);
    s.append(rules.data(), rules.size());
    s += "\n\n---\n\n";
    return s;
}

std::string ContextBuilder::build_soul() const {
    auto soul = store_.read_soul();
    if (soul.empty()) return {};
    std::string s;
    s.reserve(soul.size() + 16);
    s.append(soul.data(), soul.size());
    s += "\n\n---\n\n";
    return s;
}

std::string ContextBuilder::build_user() const {
    auto info = store_.read_user();
    if (info.empty()) return {};
    std::string s;
    s.reserve(info.size() + 32);
    s += "## User\n\n";
    s.append(info.data(), info.size());
    s += "\n\n---\n\n";
    return s;
}

std::string ContextBuilder::build_memory() const {
    auto mem = store_.read_memory();
    if (mem.empty()) return {};
    auto sv = std::string_view(mem.data(), mem.size());
    if (!has_visible_content(sv)) return {};
    std::string s;
    s.reserve(mem.size() + 32);
    s += "## Long-term Memory\n\n";
    s.append(mem.data(), mem.size());
    s += "\n\n";
    return s;
}

std::string ContextBuilder::build_workspace() const {
    std::string s;
    if (!project_dir_.empty()) {
        s += "## Current Workspace\n\nProject path: ";
        s += project_dir_.string();
        s += "\n";
    }
    if (inject_project_doc_) {
        auto doc = find_project_doc(project_dir_);
        if (!doc.empty()) {
            s += "\n\n---\n\n";
            s += doc;
        }
    }
    return s;
}

std::string ContextBuilder::build_mode(PromptMode mode) const {
    switch (mode) {
    case PromptMode::plan_reviewing:
        return
            "\n## Plan Mode — Reviewing\n"
            "You are in the planning phase. Your ONLY job is to understand the request, "
            "gather context, explore the codebase, and produce a clear plan.\n\n"
            "Rules:\n"
            "- DO NOT implement, modify, or execute anything beyond information gathering.\n"
            "- DO NOT write production code, apply fixes, run migrations, or make changes.\n"
            "- You MAY read files, search code, ask questions, and write the plan to PLAN.md.\n"
            "- Keep the plan actionable and concise: what, why, how, risks, order of steps.\n"
            "- The user will review and `/approve` when ready. Do not proceed until approved.\n";

    case PromptMode::plan_executing:
        return
            "\n## Plan Mode — Executing\n"
            "You are executing the approved plan. Follow each item in order.\n"
            "- Work through the plan step by step. Report progress after each.\n"
            "- If you encounter blockers, pause and ask before deviating from the plan.\n";

    default:
        return {};
    }
}

// ─── Token 估算 ─────────────────────────────────────────────────────────

int64_t ContextBuilder::estimate_messages_tokens(
    const llm::ConversationHistory& history) {
    int64_t total = 0;
    for (const auto& msg : history.messages()) {
        total += estimate_text_tokens(
            std::string_view(msg.get_all_text().data(), msg.get_all_text().size()));
    }
    return total;
}

int64_t ContextBuilder::estimate_text_tokens(std::string_view text) {
    // 启发式：英文 ~4 char/token，CJK ~1.5 char/token
    int64_t tokens = 0;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            tokens += (i + 3 < text.size()) ? 1 : 1;  // 4 ASCII ~= 1 token
            i += 4;
        } else {
            tokens += 1;  // 1 CJK char ~= 1 token
            // 跳过 UTF-8 多字节序列
            if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else ++i;
        }
    }
    return tokens;
}

}  // namespace ben_gear::memory
