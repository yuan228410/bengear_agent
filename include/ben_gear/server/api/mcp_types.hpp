#pragma once

#include "ben_gear/server/api/common.hpp"

#include <string>

namespace ben_gear::server {

using GetMcpStatusFn = std::function<std::string()>;

struct McpService {
    GetMcpStatusFn get_status;
};

} // namespace ben_gear::server
