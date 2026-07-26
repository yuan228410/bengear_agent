#pragma once

#include "team/types.hpp"

#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ben_gear::team {

/// 决策记录
struct DecisionRecord {
    std::string agent_id;
    std::string stage_id;
    std::string summary;
    std::chrono::system_clock::time_point timestamp;
};

/// 共享上下文（黑板模式）
///
/// Agent 间通过 artifacts/ 目录和内存中的上下文交换信息。
/// 所有写操作都被记录，支持审计。
class TeamContext {
public:
    /// 向黑板写入工作成果
    void publish(const std::string& key, const std::string& value);

    /// 从黑板读取工作成果
    std::optional<std::string> read(const std::string& key) const;

    /// 列出所有已发布的 key
    std::vector<std::string> list_keys() const;

    /// 记录决策
    void record_decision(const std::string& agent_id,
                         const std::string& stage_id,
                         const std::string& summary);

    /// 获取决策记录
    std::vector<DecisionRecord> decisions() const;

    /// 设置/获取当前阶段
    void set_current_stage(const std::string& stage_id);
    std::string current_stage() const;

    /// 快照（用于展示/序列化，不绑定 UI 格式）
    struct Snapshot {
        std::vector<std::pair<std::string, std::string>> artifacts;
        std::vector<DecisionRecord> decisions;
        std::string current_stage;
        size_t artifact_count() const { return artifacts.size(); }
    };

    Snapshot snapshot() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> artifacts_;
    std::vector<DecisionRecord> decisions_;
    std::string current_stage_;
};

} // namespace ben_gear::team
