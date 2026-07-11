# 应用层：命令、治理、运行时执行、安全代码变更

bengear_module(bengear_application
    command_descriptor_factory.cpp
    command_governance.cpp
    command_pipeline.cpp
    patch_use_cases.cpp
    safe_code_change_service.cpp
    runtime_execution.cpp
    workspace_resolver.cpp
)
target_link_libraries(bengear_application PUBLIC
    bengear_base
    bengear_json
    bengear_core
    bengear_domain
    bengear_workspace
    bengear_patch
    bengear_git
    bengear_checkpoint
    bengear_test_loop
)
