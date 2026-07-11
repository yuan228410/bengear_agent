# LLM 与集成层

bengear_module(bengear_llm
    adapter.cpp
    cooldown_tracker.cpp
)
target_link_libraries(bengear_llm PUBLIC bengear_base bengear_json bengear_acp)

bengear_module(bengear_skill
    skill/skill.cpp
    skill/zip_extract.cpp
)
target_link_libraries(bengear_skill PUBLIC bengear_base bengear_json bengear_net bengear_compress)

bengear_module(bengear_mcp
    mcp/mcp_client.cpp
)
target_link_libraries(bengear_mcp PUBLIC bengear_base bengear_json bengear_net bengear_tool)
target_compile_definitions(bengear_mcp PRIVATE BEN_GEAR_VERSION="${PROJECT_VERSION}")
