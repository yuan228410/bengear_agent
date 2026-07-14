# 基础层：底层工具、JSON、压缩、网络、运行时边界
#
# 注意：container/string 与 memory/pool 必须作为独立叶子库存在，
# 否则 bengear_json 依赖 bengear_base、bengear_base 又依赖 bengear_json
# 会形成静态库循环依赖，导致 MSVC 链接时符号丢失。

bengear_module(bengear_container_string
    container/string.cpp
)

bengear_module(bengear_memory_pool
    memory/pool.cpp
)

bengear_module(bengear_json
    json/json.cpp
    json/json_dom.cpp
    json/json_parser.cpp
    json/json_serializer.cpp
    json/json_simd.cpp
)
target_link_libraries(bengear_json PUBLIC bengear_container_string bengear_memory_pool)

bengear_module(bengear_base
    concurrency/thread_pool.cpp
    platform/platform.cpp
    platform/crash_handler.cpp
    platform/dynamic_library.cpp
    platform/random.cpp
    platform/crypto.cpp
    platform/terminal.cpp
    config/loader.cpp
    log/logger.cpp
)
target_link_libraries(bengear_base PUBLIC
    Threads::Threads
    bengear_json
    bengear_container_string
    bengear_memory_pool
)
target_link_libraries(bengear_base PRIVATE
    sqlite3
)
if(WIN32)
    target_link_libraries(bengear_base PUBLIC bcrypt)
else()
    target_link_libraries(bengear_base PUBLIC mbedcrypto ${CMAKE_DL_LIBS})
endif()

bengear_module(bengear_compress
    compress/compress_engine.cpp
    compress/zlib_engine.cpp
)
target_link_libraries(bengear_compress PUBLIC bengear_base ${BENGEAR_COMPRESS_LIBS})
target_compile_definitions(bengear_compress PUBLIC ${BENGEAR_COMPRESS_DEFINITIONS})
if(COMPRESS_BACKEND STREQUAL "zlib")
    target_include_directories(bengear_compress PUBLIC ${BENGEAR_ROOT}/third_party/zlib)
endif()

bengear_module(bengear_core
    core/runtime_boundary.cpp
)
target_link_libraries(bengear_core PUBLIC bengear_base bengear_json)

# ── 网络层（原 net/ 目录，合并到 base CMakeLists.txt）─────────────

bengear_module(bengear_net
    net/event_loop.cpp
    net/http.cpp
    net/socket.cpp
    net/wakeup_fd.cpp
    net/tcp_stream.cpp
    net/connection_pool.cpp
    net/tls/tls_engine.cpp
    ${BENGEAR_TLS_SOURCES}
)
target_include_directories(bengear_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/net/tls)
target_link_libraries(bengear_net PUBLIC bengear_base ${BENGEAR_TLS_LIBS})
target_compile_definitions(bengear_net PUBLIC ${BENGEAR_TLS_DEFINITIONS})
if(WIN32)
    target_link_libraries(bengear_net PUBLIC ws2_32 mswsock)
endif()
