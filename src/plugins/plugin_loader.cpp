#include "plugins/plugin_loader.hpp"

#include "log/logger.hpp"

#include <filesystem>
#include <system_error>

namespace ben_gear::plugins {

namespace container = base::container;
namespace platform = base::platform;

std::pair<size_t, std::vector<std::string>> PluginLoader::load_all() {
    if (plugins_dir_.empty() || !std::filesystem::exists(plugins_dir_)) {
        log::info_fmt("plugin loader: dir not found or empty: {}", plugins_dir_.string());
        return {0, {}};
    }

    size_t loaded = 0;
    std::vector<std::string> errors;

    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir_)) {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        if (path.extension().string() != platform::plugin_extension()) continue;

        auto result = load_plugin(path);
        if (result.ok()) {
            ++loaded;
            log::info_fmt("plugin loaded: {}", path.filename().string());
        } else {
            errors.push_back(path.filename().string() + ": " + result.error().message);
        }
    }

    log::info_fmt("plugin loader: loaded {} plugins, {} errors", loaded, errors.size());
    return {loaded, errors};
}

void PluginLoader::unload_all() {
    for (auto& plugin : loaded_plugins_) {
        auto shutdown_fn = reinterpret_cast<void(*)()>(
            platform::shared_library_symbol(plugin.handle, "ben_gear_plugin_shutdown"));
        if (shutdown_fn) {
            try {
                shutdown_fn();
            } catch (const std::exception& e) {
                log::error_fmt("plugin shutdown threw: {}", e.what());
            } catch (...) {
                log::error_fmt("plugin shutdown threw unknown exception");
            }
        }
        platform::shared_library_unload(plugin.handle);
    }
    loaded_plugins_.clear();
}

domain::AppResult<void> PluginLoader::load_plugin(const std::filesystem::path& path) {
    auto handle = platform::shared_library_load(path.string().c_str());
    if (!handle) {
        return domain::AppResult<void>::failure(domain::AppError::internal(
            std::string("plugin_load_failed"),
            ("dlopen failed: " + platform::shared_library_error())));
    }

    // 查找工具注册函数（必需）
    auto tools_fn = reinterpret_cast<PluginToolsFn>(
        platform::shared_library_symbol(handle, "ben_gear_plugin_tools"));
    if (!tools_fn) {
        platform::shared_library_unload(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            std::string("missing_tools_fn"),
            std::string("ben_gear_plugin_tools not found")));
    }

    int tool_count = 0;
    const BenGearTool* tools = nullptr;
    try {
        tools = tools_fn(&tool_count);
    } catch (const std::exception& e) {
        platform::shared_library_unload(handle);
        return domain::AppResult<void>::failure(domain::AppError::internal(
            std::string("tools_fn_exception"), std::string(e.what())));
    }

    // 收集元数据
    std::string info_json;
    auto info_fn = reinterpret_cast<const char*(*)()>(
        platform::shared_library_symbol(handle, "plugin_info"));
    if (info_fn) {
        try {
            const char* raw = info_fn();
            if (raw) info_json = raw;
        } catch (...) { log::debug_fmt("plugin info() threw, ignoring metadata"); }
    }

    LoadedPlugin plugin;
    plugin.handle = handle;
    plugin.info_json = std::move(info_json);
    if (tools && tool_count > 0) {
        plugin.tools.assign(tools, tools + tool_count);
    }
    loaded_plugins_.push_back(std::move(plugin));

    return domain::AppResult<void>::success();
}

} // namespace ben_gear::plugins
