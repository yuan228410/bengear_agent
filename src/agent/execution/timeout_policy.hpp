#pragma once

#include <chrono>
#include <string>
#include <unordered_map>

namespace ben_gear::agent::execution {

/// 工具超时策略接口
///
/// 将硬编码的超时配置提取为可替换的策略，支持配置化和测试。
class IToolTimeoutPolicy {
public:
    virtual ~IToolTimeoutPolicy() = default;

    /// 获取指定工具的超时时间
    /// @param tool_name 工具名称
    /// @return 超时时间（毫秒），返回 0 表示使用默认值
    virtual std::chrono::milliseconds get_timeout(
        const std::string& tool_name) const = 0;

    /// 获取默认超时时间
    virtual std::chrono::milliseconds default_timeout() const = 0;
};

/// 默认超时策略 — 基于配置的实现
class DefaultTimeoutPolicy : public IToolTimeoutPolicy {
public:
    DefaultTimeoutPolicy(
        std::chrono::milliseconds default_timeout = std::chrono::milliseconds(30000),
        std::unordered_map<std::string, std::chrono::milliseconds> overrides = {})
        : default_timeout_(default_timeout)
        , overrides_(std::move(overrides)) {}

    std::chrono::milliseconds get_timeout(const std::string& tool_name) const override {
        auto it = overrides_.find(tool_name);
        return it != overrides_.end() ? it->second : default_timeout_;
    }

    std::chrono::milliseconds default_timeout() const override {
        return default_timeout_;
    }

    /// 添加工具特定超时
    void add_override(const std::string& tool_name, std::chrono::milliseconds timeout) {
        overrides_[tool_name] = timeout;
    }

private:
    std::chrono::milliseconds default_timeout_;
    std::unordered_map<std::string, std::chrono::milliseconds> overrides_;
};

} // namespace ben_gear::agent::execution
