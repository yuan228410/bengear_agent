#pragma once

#include "base/config/settings.hpp"
#include "llm/provider_client.hpp"
#include "llm/provider_error.hpp"

#include <functional>
#include <mutex>
#include <unordered_map>

namespace ben_gear::llm {

/// Provider client factory function type
using ProviderFactory = std::function<ProviderClient::ClientFns(const config::Settings&, std::shared_ptr<net::HttpClient>)>;

/// 全局 Provider 注册表
/// 将 Provider 枚举映射到工厂函数，消除魔法字符串与硬编码 switch
class ProviderRegistry {
public:
    static ProviderRegistry& instance() {
        static ProviderRegistry registry;
        return registry;
    }

    /// 注册 Provider 工厂
    /// 线程安全：仅在程序启动/单元测试中调用
    void register_provider(config::Provider provider, ProviderFactory factory) {
        std::lock_guard lock(mutex_);
        factories_[provider] = std::move(factory);
    }

    /// 获取 Provider 工厂（必须已注册）
    ProviderFactory get_factory(config::Provider provider) const {
        std::lock_guard lock(mutex_);
        auto it = factories_.find(provider);
        if (it == factories_.end()) {
            throw ProviderError(ProviderErrorKind::unknown, 0,
                               std::string("Provider not registered: ")
                                   + std::to_string(static_cast<int>(provider)));
        }
        return it->second;
    }

    /// 检查 Provider 是否已注册
    bool has_provider(config::Provider provider) const {
        std::lock_guard lock(mutex_);
        return factories_.find(provider) != factories_.end();
    }

    /// 清空注册表（仅用于测试）
    void clear() {
        std::lock_guard lock(mutex_);
        factories_.clear();
    }

private:
    ProviderRegistry() = default;
    mutable std::mutex mutex_;
    std::unordered_map<config::Provider, ProviderFactory> factories_;
};

/// 内置 Provider 注册（在 static init 时自动注册）
/// 避免在 provider_client.cpp 中硬编码 AnthropicClient / OpenAiClient 逻辑
class ProviderRegistrar {
public:
    ProviderRegistrar(config::Provider provider, ProviderFactory factory) {
        ProviderRegistry::instance().register_provider(provider, std::move(factory));
    }
};

/// 注册内置 Provider 宏
#define BEN_GEAR_REGISTER_PROVIDER(provider_enum, factory_func) \
    namespace { ben_gear::llm::ProviderRegistrar registrar_##provider_enum(ben_gear::config::Provider::provider_enum, factory_func); }

} // namespace ben_gear::llm