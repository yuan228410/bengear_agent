# 智能体层：最小核心 + 插件系统
# ── 旧架构已完全移除 ─────────────────────────────────────────────
# 旧文件删除清单：agent.cpp agent.hpp agent_impl.hpp shared_resources.cpp
# shared_resources.hpp sub_agent.cpp sub_agent.hpp sub_agent_config.hpp
# event_sink.hpp plan_manager.hpp

add_subdirectory(core)    # bengear_agent_core — 最小核心
add_subdirectory(plugins) # bengear_agent_plugins — 插件系统
