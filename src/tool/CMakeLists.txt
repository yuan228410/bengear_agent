# 工具与协议层

bengear_module(bengear_tool
    types.cpp
    registry.cpp
    manager.cpp
)
target_link_libraries(bengear_tool PUBLIC bengear_base bengear_json bengear_workflow)

bengear_module(bengear_acp
    acp/core/content_block.cpp
    acp/core/message.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool)
