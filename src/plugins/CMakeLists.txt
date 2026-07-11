# Plugins 模块 — 插件加载器

bengear_module(bengear_plugins
    plugin_loader.cpp
)

target_link_libraries(bengear_plugins PUBLIC bengear_core bengear_capabilities)