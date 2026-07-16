# 智能层：工作区索引、仓库地图、代码智能

bengear_module(bengear_workspace_index
    workspace_index/workspace_index_service.cpp
    workspace_index/request_index_session.cpp
)
target_link_libraries(bengear_workspace_index PUBLIC bengear_base bengear_json)

bengear_module(bengear_repo_map
    repo_map/repo_map_service.cpp
)
target_link_libraries(bengear_repo_map PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_workspace_index
)

bengear_module(bengear_code_intel
    code_intel/code_intel_service.cpp
    code_intel/code_intelligence_index.cpp
)
target_link_libraries(bengear_code_intel PUBLIC
    bengear_base
    bengear_json
    bengear_domain
    bengear_repo_map
    bengear_workspace_index
)
