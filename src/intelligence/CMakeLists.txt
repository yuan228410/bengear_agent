# 智能层：工作区索引、仓库地图、代码智能、诊断

bengear_module(bengear_workspace_index
    ${BENGEAR_ROOT}/src/workspace_index/workspace_index_service.cpp
    ${BENGEAR_ROOT}/src/workspace_index/request_index_session.cpp
)
target_link_libraries(bengear_workspace_index PUBLIC bengear_base bengear_json)

bengear_module(bengear_repo_map
    ${BENGEAR_ROOT}/src/repo_map/repo_map_service.cpp
)
target_link_libraries(bengear_repo_map PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_git
    bengear_test_loop
    bengear_workspace_index
)

bengear_module(bengear_code_intel
    ${BENGEAR_ROOT}/src/code_intel/code_intel_service.cpp
    ${BENGEAR_ROOT}/src/code_intel/code_intelligence_index.cpp
)
target_link_libraries(bengear_code_intel PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_repo_map
    bengear_workspace_index
)

bengear_module(bengear_diagnostic_context
    ${BENGEAR_ROOT}/src/diagnostic_context/diagnostic_context_service.cpp
)
target_link_libraries(bengear_diagnostic_context PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_test_loop
    bengear_code_intel
    bengear_workspace_index
)

bengear_module(bengear_diagnostic_repair
    ${BENGEAR_ROOT}/src/diagnostic_repair/diagnostic_repair_plan_service.cpp
    ${BENGEAR_ROOT}/src/diagnostic_repair/diagnostic_repair_patch_preview_service.cpp
    ${BENGEAR_ROOT}/src/diagnostic_repair/diagnostic_repair_patch_draft_service.cpp
    ${BENGEAR_ROOT}/src/diagnostic_repair/diagnostic_repair_workflow_service.cpp
)
target_link_libraries(bengear_diagnostic_repair PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_diagnostic_context
    bengear_workspace_index
    bengear_patch
    bengear_application
    bengear_test_loop
)
