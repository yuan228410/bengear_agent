#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/base/net/tcp_stream.hpp"
#include "ben_gear/base/net/task.hpp"

namespace ben_gear::server {

HttpRequest parse_http(std::string_view raw);
net::Task<std::string> read_http_request(net::TcpStream& stream);

} // namespace ben_gear::server
