#pragma once

#include "base/domain/result.hpp"
#include "base/platform/dynamic_library.hpp"
#include "capabilities/capability_registry.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <memory>

namespace ben_gear::plugins {

/// 插件元数据（可选导出 plugin_info 时提供）
struct PluginMeta {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> capabilities;
};

/// 插件加载器 — 扫描目录加载共享库
///
/// ABI 约定（插件 .dll/.so 需导出）：
///   REQUIRED: extern "C" void ben_gear_plugin_init()
///   OPTIONAL: extern "C" PluginMeta plugin_info()         — 提供元数据
///   OPTIONAL: extern "C" void ben_gear_plugin_shutdown()  — 卸载清理
/// 插件在 ben_gear_plugin_init() 中通过 CapabilityRegistrar 注册能力
class PluginLoader {
public:
    explicit PluginLoader(std::filesystem::path plugins_dir = {})
        : plugins_dir_(std::move(plugins_dir)) {}

    /// 扫描并加载所有插件
    /// 返回 (成功加载的插件数, 错误信息列表)
    std::pair<size_t, std::vector<std::string>> load_all();

    /// 卸载所有已加载插件（依次调用 plugin_shutdown 再 dlclose）
    void unload_all();

    /// 已加载插件的元数据列表
    const std::vector<PluginMeta>& loaded_metas() const { return loaded_metas_; }

    ~PluginLoader() { unload_all(); }

    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;
    PluginLoader(PluginLoader&&) = default;
    PluginLoader& operator=(PluginLoader&&) = default;

private:
    std::filesystem::path plugins_dir_;
    std::vector<base::platform::SharedLibraryHandle> loaded_plugins_;
    std::vector<PluginMeta> loaded_metas_;

    /// 加载单个插件文件
    domain::AppResult<void> load_plugin(const std::filesystem::path& path);

    /// 卸载单个插件
    void unload_plugin(base::platform::SharedLibraryHandle handle);
};

/// 插件导出的初始化函数签名
/// 插件需定义: extern "C" void ben_gear_plugin_init();
using PluginInitFn = void(*)();

/// 可选的元数据导出: extern "C" PluginMeta plugin_info();
using PluginInfoFn = PluginMeta(*)();

/// 可选的关闭导出: extern "C" void ben_gear_plugin_shutdown();
using PluginShutdownFn = void(*)();

} // namespace ben_gear::plugins
