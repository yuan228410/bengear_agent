#include "agent/plugins/interface/agent_plugins.hpp"

#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace ben_gear::agent::plugin {

// ════════════════════════════════════════════════════════════════════
//  ExternalPlugin
// ════════════════════════════════════════════════════════════════════

ExternalPlugin::ExternalPlugin(const std::string& lib_path) {
    // 加载动态库
#if defined(_WIN32)
    handle_ = LoadLibraryExA(lib_path.c_str(), nullptr, 0);
    if (!handle_) throw std::runtime_error("failed to load: " + lib_path);
#else
    handle_ = dlopen(lib_path.c_str(), RTLD_LAZY);
    if (!handle_) throw std::runtime_error(dlerror());
#endif

    // 解析导出符号
    struct PluginInfo { const char* name; const char* ver; const char* desc;
                        const char** caps; int cap_count; };
    using GetInfoFn = PluginInfo(*)();

#if defined(_WIN32)
    auto get_info = (GetInfoFn)GetProcAddress((HMODULE)handle_, "plugin_info");
    init_fn_      = (InitFn)GetProcAddress((HMODULE)handle_, "plugin_init");
    shutdown_fn_  = (ShutdownFn)GetProcAddress((HMODULE)handle_, "plugin_shutdown");
#else
    auto get_info = (GetInfoFn)dlsym(handle_, "plugin_info");
    init_fn_      = (InitFn)dlsym(handle_, "plugin_init");
    shutdown_fn_  = (ShutdownFn)dlsym(handle_, "plugin_shutdown");
#endif

    if (!get_info) throw std::runtime_error("plugin missing plugin_info");
    if (!init_fn_) throw std::runtime_error("plugin missing plugin_init");

    auto info = get_info();
    name_ = info.name  ? info.name  : "unknown";
    version_ = info.ver ? info.ver : "0.0.0";
    desc_ = info.desc  ? info.desc : "";
    for (int i = 0; i < info.cap_count; ++i)
        caps_.emplace_back(info.caps[i]);
}

ExternalPlugin::~ExternalPlugin() {
    if (handle_) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)handle_);
#else
        dlclose(handle_);
#endif
    }
}

bool ExternalPlugin::initialize(const std::any& config, core::IPluginRegistry& registry) {
    return init_fn_ ? init_fn_(config, registry) : false;
}

void ExternalPlugin::shutdown() {
    if (shutdown_fn_) shutdown_fn_();
}

// ════════════════════════════════════════════════════════════════════
//  PluginDir
// ════════════════════════════════════════════════════════════════════

PluginDir::PluginDir(std::string dir) : dir_(std::move(dir)) {}

std::vector<std::shared_ptr<ExternalPlugin>> PluginDir::load_all() {
    std::vector<std::shared_ptr<ExternalPlugin>> loaded;
    errors_.clear();

    try {
        const auto* ext = ".dll";
#if !defined(_WIN32)
        ext = ".so";
#endif

        for (const auto& entry : std::filesystem::directory_iterator(dir_)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension().string() != ext) continue;
            try {
                loaded.push_back(std::make_shared<ExternalPlugin>(entry.path().string()));
            } catch (const std::exception& e) {
                errors_.push_back(entry.path().filename().string() + ": " + e.what());
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        errors_.emplace_back(e.what());
    }

    return loaded;
}

} // namespace ben_gear::agent::plugin
