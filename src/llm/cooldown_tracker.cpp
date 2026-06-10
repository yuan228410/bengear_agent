#include "ben_gear/llm/cooldown_tracker.hpp"

#include "ben_gear/base/log/logger.hpp"
#include <algorithm>

namespace ben_gear::llm {

bool CooldownTracker::is_in_cooldown(const std::string& model) const {
 std::lock_guard lock(mu_);
 auto it = states_.find(model);
 if (it == states_.end()) return false;
 return std::chrono::steady_clock::now() < it->second.cooldown_until;
}

void CooldownTracker::record_failure(const std::string& model, ProviderErrorKind kind,
                                      int retry_after_seconds) {
 std::lock_guard lock(mu_);
 auto& s = states_[model];

 auto now = std::chrono::steady_clock::now();
 // 超过衰减窗口则重置
 if (s.consecutive_failures > 0 && (now - s.last_failure_at) > k_failure_window_decay) {
  log::info_fmt("cooldown_tracker: [{}] failure count reset (window decay)", model);
  s.consecutive_failures = 0;
 }

 s.consecutive_failures++;
 s.last_error = kind;
 s.last_failure_at = now;

 if (retry_after_seconds > 0) {
  s.cooldown_until = now + std::chrono::seconds(retry_after_seconds);
  log::info_fmt("cooldown_tracker: [{}] failure #{} kind={}, retry_after={}s (server hint)",
                model, s.consecutive_failures, static_cast<int>(kind), retry_after_seconds);
 } else {
  s.cooldown_until = now + compute_cooldown(kind, s.consecutive_failures);
  auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
      s.cooldown_until - now).count();
  log::info_fmt("cooldown_tracker: [{}] failure #{} kind={}, cooldown={}s",
                model, s.consecutive_failures, static_cast<int>(kind), remaining);
 }
}

void CooldownTracker::record_success(const std::string& model) {
 std::lock_guard lock(mu_);
 auto it = states_.find(model);
 if (it != states_.end()) {
  log::info_fmt("cooldown_tracker: [{}] success, clearing cooldown (was failures={})",
                model, it->second.consecutive_failures);
  states_.erase(it);
 }
}

std::chrono::seconds CooldownTracker::cooldown_remaining(const std::string& model) const {
 std::lock_guard lock(mu_);
 auto it = states_.find(model);
 if (it == states_.end()) return std::chrono::seconds(0);
 auto rem = std::chrono::duration_cast<std::chrono::seconds>(
  it->second.cooldown_until - std::chrono::steady_clock::now());
 return rem > std::chrono::seconds(0) ? rem : std::chrono::seconds(0);
}

int CooldownTracker::failure_count(const std::string& model) const {
 std::lock_guard lock(mu_);
 auto it = states_.find(model);
 return it == states_.end() ? 0 : it->second.consecutive_failures;
}

bool CooldownTracker::try_probe(const std::string& model) {
 std::lock_guard lock(mu_);
 auto it = states_.find(model);
 if (it == states_.end()) return true;
 auto now = std::chrono::steady_clock::now();
 if (now >= it->second.cooldown_until) return true;
 if ((now - it->second.last_probe_at) >= k_probe_interval) {
  it->second.last_probe_at = now;
  log::info_fmt("cooldown_tracker: [{}] probe allowed during cooldown", model);
  return true;
 }
 return false;
}

void CooldownTracker::reset() {
 std::lock_guard lock(mu_);
 log::info_fmt("cooldown_tracker: reset, clearing {} models", states_.size());
 states_.clear();
}

std::chrono::seconds CooldownTracker::compute_cooldown(ProviderErrorKind kind, int failure_count) {
 int base = 0;
 switch (kind) {
  case ProviderErrorKind::rate_limit:      base = 10; break;
  case ProviderErrorKind::transient:       base = 5;  break;
  case ProviderErrorKind::timeout:         base = 3;  break;
  case ProviderErrorKind::auth_error:      base = 60; break;
  case ProviderErrorKind::billing_error:   base = 300; break;
  default:                                  base = 30; break;
 }
 // 指数退避，上限 5 分钟
 int cooldown = base * (1 << (failure_count - 1));
 return std::chrono::seconds(std::min(cooldown, 300));
}

} // namespace ben_gear::llm
