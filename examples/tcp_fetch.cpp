#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"

#include <array>
#include <iostream>
#include <string>

ben_gear::net::Task<void> fetch_example(ben_gear::net::EventLoop& loop) {
    auto stream = co_await ben_gear::net::async_connect(loop, "example.com", "80");
    co_await stream.write_all("GET / HTTP/1.0\r\nHost: example.com\r\nConnection: close\r\n\r\n");

    std::array<char, 1024> buffer{};
    const auto size = co_await stream.read_some(buffer.data(), buffer.size());
    std::cout.write(buffer.data(), static_cast<std::streamsize>(size));
}

int main() {
    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    loop.run(fetch_example(loop));
}
