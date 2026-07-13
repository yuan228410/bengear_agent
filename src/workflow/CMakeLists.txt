# 工作流引擎

bengear_module(bengear_workflow
    task_types.cpp
    executor.cpp
    metrics.cpp
    approval_manager.cpp
    storage/memory_storage.cpp
    scheduler.cpp
    workflow_engine.cpp
)
target_link_libraries(bengear_workflow PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool_core
    bengear_domain
)

bengear_module(bengear_workflow_visualizer
    visualizer.cpp
)
target_link_libraries(bengear_workflow_visualizer PUBLIC
    bengear_base
    bengear_json
    bengear_workflow
)
