# LLM 与集成层

bengear_module(bengear_llm
    adapter.cpp
    anthropic_client.cpp
    conversation_history.cpp
    cooldown_tracker.cpp
    openai_client.cpp
    provider_client.cpp
)
target_link_libraries(bengear_llm PUBLIC bengear_base bengear_json bengear_net bengear_acp bengear_tool)
