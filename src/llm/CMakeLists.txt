# LLM 与集成层

bengear_module(bengear_llm
    adapter.cpp
    cooldown_tracker.cpp
    provider_client.cpp
)
target_link_libraries(bengear_llm PUBLIC bengear_base bengear_json bengear_acp)
