# 应用层：命令、治理、运行时执行

bengear_module(bengear_application
    command_descriptor_factory.cpp
    command_governance.cpp
    command_pipeline.cpp
    runtime_execution.cpp
    workspace_resolver.cpp
)
target_link_libraries(bengear_application PUBLIC
    bengear_base
    bengear_json
    bengear_core
    bengear_domain
    bengear_workspace
)
