# ── Agent Runtime — 运行时层（替代旧 SharedResources）─────────────

bengear_module(bengear_agent_runtime
    runtime.cpp
    runtime_run_session.cpp
)

target_include_directories(bengear_agent_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
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
    bengear_plugins
    bengear_agent_core
)
