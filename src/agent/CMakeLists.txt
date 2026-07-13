# 智能体层：最小核心 + 运行时
# 注：插件加载已统一到 src/plugins/（bengear_plugins），详见 plugin_loader.hpp

add_subdirectory(core)    # bengear_agent_core — 最小核心
add_subdirectory(runtime) # bengear_agent_runtime — 运行时层（服务汇聚）
