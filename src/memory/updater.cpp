#include "memory/updater.hpp"

#include <chrono>
#include <thread>
#include "base/log/logger.hpp"

namespace ben_gear::memory {

void MemoryUpdater::update(
    const container::Vector<container::String>& round_summaries,
    std::function<std::string(const std::string&)> chat_fn) {
    if (round_summaries.empty()) return;

    auto current_memory = memory_store_.read_memory();
    auto current_rules = memory_store_.read_rules();
    auto current_soul = memory_store_.read_soul();

    std::string summaries_text;
    for (const auto& s : round_summaries) {
        summaries_text += "- ";
        summaries_text += std::string(s.data(), s.size());
        summaries_text += "\n";
    }

    std::string prompt =
        "You are a memory manager. Analyze the conversation summaries "
        "and update the memory.\n\n"
        "Current MEMORY.md:\n" +
        std::string(current_memory.data(), current_memory.size()) + "\n\n"
        "Current RULES.md:\n" +
        std::string(current_rules.data(), current_rules.size()) + "\n\n"
        "Current SOUL.md:\n" +
        std::string(current_soul.data(), current_soul.size()) + "\n\n"
        "Conversation summaries:\n" +
        summaries_text +
        "\n\n"
        "Please produce:\n"
        "<episode>Today's key records (facts, conclusions, to-dos)</episode>\n"
        "<updated_memory>Updated MEMORY.md content (or \"(no update needed)\")</updated_memory>\n"
        "<updated_rules>Updated RULES.md content (or \"(no update needed)\")</updated_rules>\n"
        "<updated_soul>Updated SOUL.md content (or \"(no update needed)\")</updated_soul>\n\n"
        "Rules:\n"
        "- MEMORY.md: store important long-term facts, project info, decisions\n"
        "- RULES.md: store behavioral rules, conventions, coding standards\n"
        "- SOUL.md: store core identity, mission, personality traits\n"
        "- Only add important, lasting information\n"
        "- Remove outdated entries\n"
        "- Keep it concise\n"
        "- Use ## sections to organize by topic\n";

    std::string response;
    for (int attempt = 1; attempt <= config_.max_retries; ++attempt) {
        try {
            response = chat_fn(prompt);
            if (!response.empty()) break;
            log::warn_fmt(
                "MemoryUpdater empty response, attempt={}/{}", attempt,
                config_.max_retries);
        } catch (const std::exception& e) {
            log::warn_fmt("MemoryUpdater failed, attempt={}/{}: {}",
                          attempt, config_.max_retries, e.what());
        }

        if (attempt < config_.max_retries) {
            std::this_thread::sleep_for(std::chrono::seconds(attempt));
        }
    }

    if (response.empty()) {
        log::error_fmt("MemoryUpdater all retries failed");
        return;
    }

    auto episode = extract_tag("episode", response);
    auto updated_memory = extract_tag("updated_memory", response);
    auto updated_rules = extract_tag("updated_rules", response);
    auto updated_soul = extract_tag("updated_soul", response);

    auto needs_update = [](const auto& content) {
        if (!content) return false;
        auto str = std::string(content->data(), content->size());
        auto lower = base::utils::to_lower(base::utils::trim(str));
        return !(lower.find("no update needed") != std::string::npos ||
                 lower.find("no updates needed") != std::string::npos ||
                 lower == "(no update needed)" || lower.empty());
    };

    if (episode) {
        episode_store_.append_today(*episode);
        log::info_fmt("MemoryUpdater: episode written, size={}", episode->size());
    }

    if (needs_update(updated_memory)) {
        memory_store_.write_memory(*updated_memory, config_.write_tier);
        log::info_fmt("MemoryUpdater: memory updated, tier={}",
                      base::TierPaths::tier_name(config_.write_tier));
    }

    if (needs_update(updated_rules)) {
        memory_store_.write_rules(*updated_rules, config_.write_tier);
        log::info_fmt("MemoryUpdater: rules updated, tier={}",
                      base::TierPaths::tier_name(config_.write_tier));
    }

    if (needs_update(updated_soul)) {
        memory_store_.write_soul(*updated_soul, base::Tier::global);
        log::info_fmt("MemoryUpdater: soul updated, tier=global");
    }
}

std::optional<container::String> MemoryUpdater::extract_tag(
    std::string_view tag, std::string_view text) const {
    auto open_tag = "<" + std::string(tag) + ">";
    auto close_tag = "</" + std::string(tag) + ">";

    auto start = text.find(open_tag);
    if (start == std::string_view::npos) return std::nullopt;
    start += open_tag.size();

    auto end = text.find(close_tag, start);
    if (end == std::string_view::npos) return std::nullopt;

    auto content = text.substr(start, end - start);

    while (!content.empty() &&
           (content.front() == '\n' || content.front() == ' ' ||
            content.front() == '\r')) {
        content.remove_prefix(1);
    }
    while (!content.empty() &&
           (content.back() == '\n' || content.back() == ' ' ||
            content.back() == '\r')) {
        content.remove_suffix(1);
    }

    if (content.empty()) return std::nullopt;
    return container::String(std::string(content).c_str());
}


MemoryUpdater::MemoryUpdater(MemoryStore& memory_store,
                             const EpisodeStore& episode_store,
                             const std::filesystem::path& session_dir,
                             Config config)
    : memory_store_(memory_store),
      episode_store_(episode_store),
      session_dir_(session_dir),
      config_(config) {
    if (config_.write_tier == base::Tier::global) {
        config_.write_tier = base::Tier::user;
    }
}

}  // namespace ben_gear::memory
