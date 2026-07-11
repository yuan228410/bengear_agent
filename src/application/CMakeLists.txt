# 应用层：命令、补丁、工作区解析等用例

bengear_module(bengear_application
    ${BENGEAR_ROOT}/src/application/command_descriptor_factory.cpp
    ${BENGEAR_ROOT}/src/application/command_governance.cpp
    ${BENGEAR_ROOT}/src/application/command_pipeline.cpp
    ${BENGEAR_ROOT}/src/application/patch_use_cases.cpp
    ${BENGEAR_ROOT}/src/application/safe_code_change_service.cpp
    ${BENGEAR_ROOT}/src/application/runtime_execution.cpp
    ${BENGEAR_ROOT}/src/application/workspace_resolver.cpp
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
