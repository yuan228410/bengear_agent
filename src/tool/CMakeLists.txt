# 工具与协议层

bengear_module(bengear_tool
    types.cpp
    registry.cpp
    manager.cpp
    builtin_tools.cpp
    workflow_tools.cpp
    history_tools.cpp
    skill_tools.cpp
    memory_tools.cpp
    permission_tools.cpp
    patch_tools.cpp
    git_tools.cpp
    checkpoint_tools.cpp
    test_loop_tools.cpp
    repo_map_tools.cpp
    code_intel_tools.cpp
    diagnostic_context_tools.cpp
    diagnostic_repair_tools.cpp
)
target_link_libraries(bengear_tool PUBLIC
    bengear_base
    bengear_json
    bengear_workflow
    bengear_workflow_visualizer
    bengear_net
    bengear_skill
    bengear_memory
    bengear_workspace
    bengear_llm
    bengear_application
    bengear_capabilities
    bengear_git
    bengear_checkpoint
    bengear_test_loop
    bengear_permission
    bengear_patch
    bengear_repo_map
    bengear_code_intel
    bengear_workspace_index
    bengear_diagnostic
)

bengear_module(bengear_acp
    acp/core/content_block.cpp
    acp/core/message.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool)
