#pragma once

#include "base/tier_paths.hpp"
#include <vector>
// fwd
namespace ben_gear::memory {
class MemoryStore;
class EpisodeStore;
}  // namespace ben_gear::memory

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace ben_gear::memory {


/// LLM 驱动的记忆更新器
class MemoryUpdater {
public:
    struct Config {
        base::Tier write_tier;
        int max_retries = 3;

        Config() : write_tier(base::Tier::workspace) {}
        explicit Config(base::Tier tier) : write_tier(tier) {}
    };

    MemoryUpdater(MemoryStore& memory_store,
                  const EpisodeStore& episode_store,
                  const std::filesystem::path& session_dir,
                  Config config = Config());

    /// 根据轮次摘要更新记忆
    void update(
        const std::vector<std::string>& round_summaries,
        std::function<std::string(const std::string&)> chat_fn);

private:
    std::optional<std::string> extract_tag(
        std::string_view tag, std::string_view text) const;

    MemoryStore& memory_store_;
    const EpisodeStore& episode_store_;
    std::filesystem::path session_dir_;
    Config config_;
};

}  // namespace ben_gear::memory
