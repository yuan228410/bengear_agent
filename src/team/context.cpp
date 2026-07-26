#include "team/context.hpp"

namespace ben_gear::team {

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

void TeamContext::record_decision(const std::string& agent_id,
                                   const std::string& stage_id,
                                   const std::string& summary) {
    std::unique_lock lock(mutex_);
    decisions_.push_back({agent_id, stage_id, summary,
                          std::chrono::system_clock::now()});
}

std::vector<DecisionRecord> TeamContext::decisions() const {
    std::shared_lock lock(mutex_);
    return decisions_;  // 拷贝返回，避免引用悬空
}

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
