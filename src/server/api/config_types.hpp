#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct ConfigInfo {
    std::string model;
    std::string provider;
    std::string workspace;
    std::string display_name;
    std::string version;
};

using GetConfigFn = std::function<ConfigInfo()>;
using SetModelFn = std::function<void(const std::string& model)>;

struct ConfigService {
    GetConfigFn get_config;
    SetModelFn set_model;
};

} // namespace ben_gear::server
