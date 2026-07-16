#pragma once

#include "server/api/common.hpp"

#include <memory>
#include <string>

namespace ben_gear::server {

class McpService {
public:
    virtual ~McpService() = default;

    virtual std::string get_status() = 0;
};

} // namespace ben_gear::server
