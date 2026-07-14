# 网络层：事件循环、Socket、连接池、TLS 引擎（头文件与实现同目录）

bengear_module(bengear_net
    event_loop.cpp
    http.cpp
    socket.cpp
    wakeup_fd.cpp
    tcp_stream.cpp
    connection_pool.cpp
    tls/tls_engine.cpp
    ${BENGEAR_TLS_SOURCES}
)
target_include_directories(bengear_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tls)
target_link_libraries(bengear_net PUBLIC bengear_base ${BENGEAR_TLS_LIBS})
target_compile_definitions(bengear_net PUBLIC ${BENGEAR_TLS_DEFINITIONS})
if(WIN32)
    target_link_libraries(bengear_net PUBLIC ws2_32 mswsock)
endif()
