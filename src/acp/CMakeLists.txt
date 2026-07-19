bengear_module(bengear_acp
    core/content_block.cpp
    core/message.cpp
    types/tool_call_types.cpp
)
target_link_libraries(bengear_acp PUBLIC bengear_base bengear_json bengear_tool_core)
target_include_directories(bengear_acp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
