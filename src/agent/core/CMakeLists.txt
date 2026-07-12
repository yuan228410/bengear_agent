# ── Agent Core — 最小核心 ─────────────────────────────────────────

bengear_module(bengear_agent_core
    agent_core.cpp
    default_services.cpp
    event_sink.cpp
)

target_include_directories(bengear_agent_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/interface>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(bengear_agent_core PUBLIC
    bengear_base
    bengear_json
    bengear_container_string
)

if(WIN32)
    target_link_libraries(bengear_agent_core PUBLIC bcrypt)
endif()

# ── Test ───────────────────────────────────────────────────────────
if(BEN_GEAR_BUILD_TESTS)
    add_executable(agent_core_test test_main.cpp)
    target_link_libraries(agent_core_test PRIVATE bengear_agent_core)
    add_test(NAME agent_core_test COMMAND agent_core_test)
endif()
