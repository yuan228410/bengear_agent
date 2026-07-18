# 智能体层：最小核心 + 运行时

bengear_module(bengear_agent_core
    core/agent_core.cpp
    core/default_services.cpp
)

target_include_directories(bengear_agent_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
)

target_link_libraries(bengear_agent_core PUBLIC
    bengear_base
    bengear_json
)

if(WIN32)
    target_link_libraries(bengear_agent_core PUBLIC bcrypt)
endif()

if(BEN_GEAR_BUILD_TESTS)
    add_executable(agent_core_test core/test_main.cpp)
    target_link_libraries(agent_core_test PRIVATE bengear_agent_core)
    set_property(GLOBAL APPEND PROPERTY BENGEAR_TEST_TARGETS agent_core_test)
endif()

bengear_module(bengear_execution
    execution/loop.cpp
    execution/interceptors/plan_interceptor.cpp
    execution/interceptors/compaction_interceptor.cpp
)

target_include_directories(bengear_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/execution>
)

target_link_libraries(bengear_execution PUBLIC
    bengear_base
    bengear_json
    bengear_acp
    bengear_net
    bengear_tool
    bengear_llm
    bengear_memory
    bengear_orchestration
    bengear_agent_core
)


bengear_module(bengear_agent_runtime
    runtime/runtime.cpp
    runtime/runtime_run_session.cpp
    runtime/sub_agent_runtime.cpp
    runtime/lifecycle_manager.cpp
    runtime/application/command_descriptor_factory.cpp
    runtime/application/command_governance.cpp
    runtime/application/command_pipeline.cpp
    runtime/application/runtime_execution.cpp
    runtime/application/workspace_resolver.cpp
)

target_include_directories(bengear_agent_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime>
)

target_link_libraries(bengear_agent_runtime PUBLIC
    bengear_base
    bengear_json
    bengear_acp
    bengear_net
    bengear_tool
    bengear_domain
    bengear_workspace
    bengear_llm
    bengear_orchestration
    bengear_mcp
    bengear_skill
    bengear_memory
    bengear_workflow
    bengear_plugins
    bengear_execution
    bengear_agent_core
)
