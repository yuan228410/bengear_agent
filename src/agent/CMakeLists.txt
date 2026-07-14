# 智能体层：最小核心 + 运行时
# 注：插件加载已统一到 src/plugins/（bengear_plugins），详见 plugin_loader.hpp

# ── Agent Core — 最小核心 ─────────────────────────────────────────

bengear_module(bengear_agent_core
    core/agent_core.cpp
    core/default_services.cpp
    core/event_sink.cpp
)

target_include_directories(bengear_agent_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core/interface>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
)

target_link_libraries(bengear_agent_core PUBLIC
    bengear_base
    bengear_json
    bengear_container_string
)

if(WIN32)
    target_link_libraries(bengear_agent_core PUBLIC bcrypt)
endif()

# ── Test ───────────────────────────────────────────────────────────
if(BEN_GEAR_BUILD_TESTS)
    add_executable(agent_core_test core/test_main.cpp)
    target_link_libraries(agent_core_test PRIVATE bengear_agent_core)
    set_property(GLOBAL APPEND PROPERTY BENGEAR_TEST_TARGETS agent_core_test)
endif()

# ── Agent Runtime — 运行时层（替代旧 SharedResources）─────────────

bengear_module(bengear_agent_runtime
    runtime/runtime.cpp
    runtime/runtime_run_session.cpp
)

target_include_directories(bengear_agent_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime>
)

target_link_libraries(bengear_agent_runtime PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_llm
    bengear_orchestration
    bengear_application
    bengear_mcp
    bengear_skill
    bengear_memory
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_audit
    bengear_workspace_index
    bengear_repo_map
    bengear_code_intel
    bengear_diagnostic
    bengear_workflow
    bengear_workflow_visualizer
    bengear_plugins
    bengear_agent_core
)
