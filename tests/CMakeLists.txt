function(bengear_test name libs)
    add_executable(${name} ${ARGN})
    target_precompile_headers(${name} PRIVATE ${BENGEAR_ROOT}/src/bengear_pch.hpp)
    target_link_libraries(${name} PRIVATE ${libs})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    bengear_apply_warnings(${name})
    set_target_properties(${name} PROPERTIES FOLDER "tests")
    if(MSVC)
        target_link_options(${name} PRIVATE /STACK:16777216)
    elseif(WIN32)
        target_link_options(${name} PRIVATE -Wl,--stack,16777216)
    endif()
    # 收集到全局列表，由根 CMakeLists.txt 创建 run_tests target
    set_property(GLOBAL APPEND PROPERTY BENGEAR_TEST_TARGETS ${name})
endfunction()

bengear_test(test_base bengear_test_runtime
    test_main.cpp test_container.cpp test_json.cpp test_config.cpp
    test_base_utils.cpp test_file_lock.cpp test_domain_event.cpp
    test_core_runtime_boundary.cpp test_service_registry.cpp
)

bengear_test(test_net bengear_test_runtime
    test_main.cpp test_net.cpp test_pool.cpp test_failover.cpp
)

bengear_test(test_llm bengear_test_runtime
    test_main.cpp test_llm_clients.cpp test_llm_stream.cpp
    test_llm_retry.cpp test_llm_endpoint.cpp test_usage.cpp
    test_context_pruner.cpp test_context_overflow.cpp test_provider_interface.cpp
)

bengear_test(test_tool bengear_test_runtime
    test_main.cpp test_tool.cpp test_acp.cpp test_plugins.cpp
)
target_compile_definitions(test_tool PRIVATE
    BENGEAR_PLUGIN_TEST_DIR="${CMAKE_BINARY_DIR}/examples/plugins/hello_capability"
    BENGEAR_PLUGIN_TEST_SUFFIX="${CMAKE_SHARED_MODULE_SUFFIX}"
)

bengear_test(test_memory_workspace bengear_test_runtime
    test_main.cpp test_memory.cpp test_memory_episode.cpp
    test_workspace.cpp test_session.cpp test_history_db.cpp
)

bengear_test(test_orchestration bengear_test_runtime
    test_main.cpp test_orchestration.cpp
)

bengear_test(test_mcp bengear_test_runtime
    test_main.cpp test_mcp.cpp
)

bengear_test(test_agent_server bengear_test_runtime
    test_main.cpp test_agent.cpp
    test_plan_mode.cpp
    test_sub_agent.cpp test_lifecycle.cpp test_integration.cpp
)

# 运行全部测试：cmake --build build --target run_tests
# 构建但勿运行：cmake --build build --target bengear_tests_build
# 单个测试：./build/tests/test_base [--verbose] [--xml=result.xml]

get_property(_all_test_targets GLOBAL PROPERTY BENGEAR_TEST_TARGETS)

add_custom_target(bengear_tests_build
    DEPENDS ${_all_test_targets}
    COMMENT "All test targets built"
)

# CMake 脚本入口：通过 -D 传参到 run_tests.cmake
# 将分号分隔的 CMake 列表转为空格分隔，避免被 shell 当作命令分隔符
string(REPLACE ";" " " _targets_str "${_all_test_targets}")
add_custom_target(run_tests
    COMMAND ${CMAKE_COMMAND} -D "BENGEAR_TEST_TARGETS=${_targets_str}"
            -D "EXE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            -D "BINARY_DIR=${CMAKE_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_tests.cmake"
    DEPENDS ${_all_test_targets}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all tests..."
)
