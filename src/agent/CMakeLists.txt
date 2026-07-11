# 智能体层：核心、子智能体、共享资源（已合并为一个模块）

bengear_module(bengear_agent
    ${BENGEAR_ROOT}/src/agent/agent.cpp
    ${BENGEAR_ROOT}/src/agent/sub_agent.cpp
    ${BENGEAR_ROOT}/src/agent/shared_resources.cpp
)
target_link_libraries(bengear_agent PUBLIC
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
)
