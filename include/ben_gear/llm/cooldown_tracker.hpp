#pragma once

#include "ben_gear/llm/provider_error.hpp"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace ben_gear::llm {

/// per-model 冷却追踪器
///
/// 主模型 429/500 后进入冷却，自动切换到备用模型；
/// 冷却结束后主模型恢复可用。探针机制允许冷却期内偶尔试探。
class CooldownTracker {
public:
 /// 指定 model 是否在冷却期
 bool is_in_cooldown(const std::string& model) const;

 /// 记录失败：根据错误类型 + 连续失败次数计算冷却时长
 void record_failure(const std::string& model, ProviderErrorKind kind,
                     int retry_after_seconds = 0);

 /// 记录成功：清除冷却
 void record_success(const std::string& model);

 /// 剩余冷却秒数
 std::chrono::seconds cooldown_remaining(const std::string& model) const;

 /// 连续失败次数
 int failure_count(const std::string& model) const;

 /// 探针：冷却期内每 30s 允许一次试探
 bool try_probe(const std::string& model);

 /// 清除所有状态
 void reset();

 static constexpr std::chrono::seconds k_probe_interval{30};

private:
 static constexpr std::chrono::hours k_failure_window_decay{24};

 struct State {
  int consecutive_failures = 0;
  ProviderErrorKind last_error = ProviderErrorKind::unknown;
  std::chrono::steady_clock::time_point cooldown_until;
  std::chrono::steady_clock::time_point last_failure_at;
  std::chrono::steady_clock::time_point last_probe_at;
 };

 static std::chrono::seconds compute_cooldown(ProviderErrorKind kind, int failure_count);

 mutable std::mutex mu_;
 std::unordered_map<std::string, State> states_;
};

} // namespace ben_gear::llm
