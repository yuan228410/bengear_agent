#pragma once

#include <string>
#include <memory>
#include <vector>

#include "agent/core/interface/agent_core.hpp"

namespace ben_gear::agent::plugin {

/// 外部插件（通过动态库加载）
class ExternalPlugin : public core::IAgentPlugin {
public:
    explicit ExternalPlugin(const std::string& lib_path);
    ~ExternalPlugin() override;

    std::string name() const override { return name_; }
    std::string version() const override { return version_; }
    std::string description() const override { return desc_; }
    core::PluginType plugin_type() const override { return core::PluginType::integration; }
    std::vector<std::string> capabilities() const override { return caps_; }

    bool initialize(const std::any& config, core::IPluginRegistry& registry) override;
    void shutdown() override;

private:
    std::string name_;
    std::string version_;
    std::string desc_;
    std::vector<std::string> caps_;
    void* handle_ = nullptr;

    using InitFn     = bool(*)(const std::any&, core::IPluginRegistry&);
    using ShutdownFn = void(*)();
    InitFn     init_fn_     = nullptr;
    ShutdownFn shutdown_fn_ = nullptr;
};

/// 从目录批量加载插件
class PluginDir {
public:
    explicit PluginDir(std::string dir);
    std::vector<std::shared_ptr<ExternalPlugin>> load_all();
    std::vector<std::string> errors() const { return errors_; }

private:
    std::string dir_;
    std::vector<std::string> errors_;
};

} // namespace ben_gear::agent::plugin
