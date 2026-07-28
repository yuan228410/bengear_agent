#include "team/context.hpp"

namespace ben_gear::team {

// ─── 黑板 ────────────────────────────────────────────────────────

void TeamContext::publish(const std::string& key, const std::string& value) {
    std::unique_lock lock(mutex_);
    artifacts_[key] = value;
}

std::optional<std::string> TeamContext::read(const std::string& key) const {
    std::shared_lock lock(mutex_);
    auto it = artifacts_.find(key);
    if (it != artifacts_.end()) return it->second;
    return std::nullopt;
}

std::vector<std::string> TeamContext::list_keys() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> keys;
    keys.reserve(artifacts_.size());
    for (const auto& [k, _] : artifacts_) keys.push_back(k);
    return keys;
}

// ─── 决策记录 ────────────────────────────────────────────────────

void TeamContext::record_decision(const std::string& agent_id,
                                   const std::string& stage_id,
                                   const std::string& summary) {
    std::unique_lock lock(mutex_);
    decisions_.push_back({agent_id, stage_id, summary,
                          std::chrono::system_clock::now()});
}

std::vector<DecisionRecord> TeamContext::decisions() const {
    std::shared_lock lock(mutex_);
    return decisions_;
}

// ─── 消息传递 ────────────────────────────────────────────────────

void TeamContext::send_message(const std::string& from,
                                const std::string& to,
                                const std::string& subject,
                                const std::string& body) {
    std::unique_lock lock(mutex_);
    TeamMessage msg;
    msg.from = from;
    msg.to = to;
    msg.subject = subject;
    msg.body = body;
    msg.timestamp = std::chrono::system_clock::now();
    inboxes_[to].push_back(std::move(msg));
}

std::vector<TeamMessage> TeamContext::read_inbox(const std::string& agent_id) {
    std::unique_lock lock(mutex_);
    auto it = inboxes_.find(agent_id);
    if (it == inboxes_.end()) return {};

    // 标记为已读但保留消息，支持多次读取
    std::vector<TeamMessage> messages;
    messages.reserve(it->second.size());
    for (auto& msg : it->second) {
        msg.read = true;
        messages.push_back(msg);
    }
    return messages;
}

size_t TeamContext::unread_count(const std::string& agent_id) const {
    std::shared_lock lock(mutex_);
    auto it = inboxes_.find(agent_id);
    if (it == inboxes_.end()) return 0;
    size_t count = 0;
    for (const auto& msg : it->second) {
        if (!msg.read) ++count;
    }
    return count;
}

std::vector<TeamMessage> TeamContext::list_conversations(
    const std::string& agent_id) const {
    std::shared_lock lock(mutex_);
    auto it = inboxes_.find(agent_id);
    if (it == inboxes_.end()) return {};
    std::vector<TeamMessage> result(it->second.begin(), it->second.end());
    return result;
}

// ─── 工作流状态 ──────────────────────────────────────────────────

void TeamContext::set_current_stage(const std::string& stage_id) {
    std::unique_lock lock(mutex_);
    current_stage_ = stage_id;
}

std::string TeamContext::current_stage() const {
    std::shared_lock lock(mutex_);
    return current_stage_;
}

TeamContext::Snapshot TeamContext::snapshot() const {
    std::shared_lock lock(mutex_);
    Snapshot snap;
    snap.artifacts.reserve(artifacts_.size());
    for (const auto& [k, v] : artifacts_) {
        snap.artifacts.emplace_back(k, v);
    }
    snap.decisions = decisions_;
    snap.current_stage = current_stage_;
    return snap;
}

} // namespace ben_gear::team
