function(bengear_test name libs)
    add_executable(${name} ${ARGN})
    target_precompile_headers(${name} PRIVATE ${BENGEAR_ROOT}/src/bengear_pch.hpp)
    target_link_libraries(${name} PRIVATE ${libs})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    bengear_apply_warnings(${name})
    if(WIN32)
        target_link_options(${name} PRIVATE -Wl,--stack,16777216)
    endif()
    add_test(NAME ${name} COMMAND ${name}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endfunction()

bengear_test(test_base bengear_test_runtime
    test_main.cpp test_container.cpp test_json.cpp test_config.cpp
    test_base_utils.cpp test_file_lock.cpp test_domain_event.cpp
    test_platform_win.cpp test_core_runtime_boundary.cpp
)

bengear_test(test_net bengear_test_runtime
    test_main.cpp test_net.cpp test_pool.cpp test_failover.cpp
)

bengear_test(test_llm bengear_test_runtime
    test_main.cpp test_llm_clients.cpp test_llm_stream.cpp
    test_llm_retry.cpp test_llm_endpoint.cpp test_usage.cpp
    test_context_pruner.cpp test_context_overflow.cpp
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

bengear_test(test_capabilities bengear_test_runtime
    test_main.cpp test_patch.cpp test_git_tools.cpp test_checkpoint.cpp
    test_test_loop.cpp test_permission.cpp test_audit_store.cpp
)

bengear_test(test_intelligence bengear_test_runtime
    test_main.cpp test_repo_map.cpp test_workspace_index.cpp
    test_code_intel.cpp test_repo_intelligence.cpp
    test_diagnostic_context.cpp test_diagnostic_repair_plan.cpp
    test_diagnostic_repair_patch_preview.cpp test_diagnostic_repair_patch_draft.cpp
    test_diagnostic_repair_workflow.cpp
)

bengear_test(test_orchestration_workflow bengear_test_runtime
    test_main.cpp test_orchestration.cpp test_workflow.cpp test_workflow_extended.cpp
)

bengear_test(test_agent_server bengear_test_runtime
    test_main.cpp test_agent.cpp test_server.cpp
    test_server_workflow_event_projection.cpp test_plan_mode.cpp
    test_sub_agent.cpp test_lifecycle.cpp test_application_architecture.cpp
    test_command_governance.cpp test_runtime_execution.cpp test_cli_runtime_presenter.cpp
)
