# 工具与协议层

bengear_module(bengear_tool
    ${BENGEAR_ROOT}/src/tool/types.cpp
    ${BENGEAR_ROOT}/src/tool/registry.cpp
    ${BENGEAR_ROOT}/src/tool/manager.cpp
    ${BENGEAR_ROOT}/src/tools/sub_agent_tools.cpp
)
target_link_libraries(bengear_tool PUBLIC bengear_base bengear_json bengear_workflow_core)

bengear_module(bengear_acp
    ${BENGEAR_ROOT}/src/acp/core/content_block.cpp
    ${BENGEAR_ROOT}/src/acp/core/message.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool)
