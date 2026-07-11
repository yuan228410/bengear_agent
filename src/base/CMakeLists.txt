# 基础层：底层工具、JSON、压缩、运行时边界、领域事件
#
# 注意：container/string 与 memory/pool 必须作为独立叶子库存在，
# 否则 bengear_json 依赖 bengear_base、bengear_base 又依赖 bengear_json
# 会形成静态库循环依赖，导致 MSVC 链接时符号丢失。

bengear_module(bengear_container_string
    ${BENGEAR_ROOT}/src/base/container/string.cpp
)

bengear_module(bengear_memory_pool
    ${BENGEAR_ROOT}/src/base/memory/pool.cpp
)

bengear_module(bengear_base
    ${BENGEAR_ROOT}/src/base/concurrency/thread_pool.cpp
    ${BENGEAR_ROOT}/src/base/platform/platform.cpp
    ${BENGEAR_ROOT}/src/config/loader.cpp
)
target_link_libraries(bengear_base PUBLIC
    sqlite3
    bengear_json
    bengear_container_string
    bengear_memory_pool
)

bengear_module(bengear_json
    ${BENGEAR_ROOT}/src/base/json/json.cpp
    ${BENGEAR_ROOT}/src/base/json/json_dom.cpp
    ${BENGEAR_ROOT}/src/base/json/json_parser.cpp
    ${BENGEAR_ROOT}/src/base/json/json_serializer.cpp
    ${BENGEAR_ROOT}/src/base/json/json_simd.cpp
)
target_link_libraries(bengear_json PUBLIC bengear_container_string bengear_memory_pool)

bengear_module(bengear_compress
    ${BENGEAR_ROOT}/src/compress/compress_engine.cpp
    ${BENGEAR_ROOT}/src/compress/zlib_engine.cpp
)
target_link_libraries(bengear_compress PUBLIC bengear_base ${BENGEAR_COMPRESS_LIBS})
target_compile_definitions(bengear_compress PUBLIC ${BENGEAR_COMPRESS_DEFINITIONS})
if(COMPRESS_BACKEND STREQUAL "zlib")
    target_include_directories(bengear_compress PUBLIC ${BENGEAR_ROOT}/third_party/zlib)
endif()

bengear_module(bengear_core
    ${BENGEAR_ROOT}/src/core/runtime_boundary.cpp
)
target_link_libraries(bengear_core PUBLIC bengear_base bengear_json)

bengear_module(bengear_domain
    ${BENGEAR_ROOT}/src/domain/event.cpp
)
target_link_libraries(bengear_domain PUBLIC bengear_base bengear_json bengear_tool)
