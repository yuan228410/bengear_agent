#pragma once

#include <string>
#include <unordered_map>

namespace ben_gear::agent::core {

/// 核心数据结构 — 配套 IFileService / ICommandExecutor 等核心服务接口
/// 纯数据 payload，不包含行为。

struct CommandResult {
    int exit_code = -1;
    std::string stdout_str;
    std::string stderr_str;
    double exec_time_ms = 0.0;

    bool success() const noexcept { return exit_code == 0; }
};

}  // namespace ben_gear::agent::core
