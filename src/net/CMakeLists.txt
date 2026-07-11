# 网络层：事件循环、Socket、连接池与 TLS 引擎

bengear_module(bengear_net
    ${BENGEAR_ROOT}/src/base/net/event_loop.cpp
    ${BENGEAR_ROOT}/src/base/net/socket.cpp
    ${BENGEAR_ROOT}/src/base/net/wakeup_fd.cpp
    ${BENGEAR_ROOT}/src/base/net/tcp_stream.cpp
    ${BENGEAR_ROOT}/src/base/net/connection_pool.cpp
    ${BENGEAR_ROOT}/src/net/tls/tls_engine.cpp
    ${BENGEAR_TLS_SOURCES}
)
target_include_directories(bengear_net PUBLIC ${BENGEAR_ROOT}/src/net/tls)
target_link_libraries(bengear_net PUBLIC bengear_base ${BENGEAR_TLS_LIBS})
target_compile_definitions(bengear_net PUBLIC ${BENGEAR_TLS_DEFINITIONS})
if(WIN32)
    target_link_libraries(bengear_net PUBLIC ws2_32 mswsock)
endif()
