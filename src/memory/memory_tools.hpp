#pragma once

#include <vector>
#include "log/logger.hpp"
#include "memory/store.hpp"
#include "memory/episode.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace ben_gear::memory {


/// 注册记忆相关工具（不含情景记忆工具）
/// 情景记忆工具需要在 Session 构造后单独注册（因为依赖 Session 的 EpisodeStore）
void register_memory_tools(capabilities::tool::ToolRegistry& tools,
                           std::shared_ptr<MemoryStore> memory_store);

/// 注册情景记忆工具（由 Session 构造后调用，因为依赖 Session 的 EpisodeStore）
void register_episode_tools(capabilities::tool::ToolRegistry& tools,
                            std::shared_ptr<EpisodeStore> episode_store);

}  // namespace ben_gear::memory
