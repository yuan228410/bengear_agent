#pragma once

#include "team/types.hpp"

#include <deque>
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

/// Agent 间消息
struct TeamMessage {
    std::string from;
    std::string to;
    std::string subject;
    std::string body;
    std::chrono::system_clock::time_point timestamp;
    bool read = false;
};

/// 共享上下文（黑板模式 + 消息传递）
class TeamContext {
public:
    // ─── 黑板 ──────────────────────────────────────────────────
    void publish(const std::string& key, const std::string& value);
    std::optional<std::string> read(const std::string& key) const;
    std::vector<std::string> list_keys() const;

    // ─── 决策记录 ──────────────────────────────────────────────
    void record_decision(const std::string& agent_id,
                         const std::string& stage_id,
                         const std::string& summary);
    std::vector<DecisionRecord> decisions() const;

    // ─── 消息传递 ──────────────────────────────────────────────
    void send_message(const std::string& from, const std::string& to,
                      const std::string& subject, const std::string& body);
    std::vector<TeamMessage> read_inbox(const std::string& agent_id);
    size_t unread_count(const std::string& agent_id) const;
    std::vector<TeamMessage> list_conversations(const std::string& agent_id) const;

    // ─── 工作流状态 ────────────────────────────────────────────
    void set_current_stage(const std::string& stage_id);
    std::string current_stage() const;

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
    std::unordered_map<std::string, std::deque<TeamMessage>> inboxes_;
};

} // namespace ben_gear::team
