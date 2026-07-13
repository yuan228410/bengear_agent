# Plugins 模块 — 插件加载器
# ABI 定义: plugin_abi.hpp（纯 C，跨平台，不依赖 capabilities 层）

bengear_module(bengear_plugins
    plugin_loader.cpp
)

target_link_libraries(bengear_plugins PUBLIC bengear_core)