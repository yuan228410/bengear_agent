# 团队 Agent 模块

bengear_module(bengear_team
    context.cpp
    loader.cpp
    agent.cpp
    orchestrator.cpp
    tools.cpp
)
target_link_libraries(bengear_team PUBLIC
    bengear_base
    bengear_json
    bengear_agent_runtime
    bengear_workspace
    bengear_tool
    bengear_llm
)
target_include_directories(bengear_team PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
