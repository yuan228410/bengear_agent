# ── Agent Plugins — 插件系统 ──────────────────────────────────────

bengear_module(bengear_agent_plugins
    agent_plugins.cpp
)

target_include_directories(bengear_agent_plugins PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(bengear_agent_plugins PUBLIC
    bengear_agent_core
)
