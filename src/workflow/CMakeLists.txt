# 工作流引擎

bengear_module(bengear_workflow
    ${BENGEAR_ROOT}/src/workflow/task_types.cpp
    ${BENGEAR_ROOT}/src/workflow/executor.cpp
    ${BENGEAR_ROOT}/src/workflow/metrics.cpp
    ${BENGEAR_ROOT}/src/workflow/approval_manager.cpp
    ${BENGEAR_ROOT}/src/workflow/storage/memory_storage.cpp
    ${BENGEAR_ROOT}/src/workflow/scheduler.cpp
    ${BENGEAR_ROOT}/src/workflow/workflow_engine.cpp
)
target_link_libraries(bengear_workflow PUBLIC
    bengear_base
    bengear_json
    bengear_net
    bengear_tool
    bengear_domain
)

bengear_module(bengear_workflow_visualizer
    ${BENGEAR_ROOT}/src/workflow/visualizer.cpp
)
target_link_libraries(bengear_workflow_visualizer PUBLIC
    bengear_base
    bengear_json
    bengear_workflow
)
