#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct ConfigInfo {
    container::String model;
    container::String provider;
    container::String workspace;
    container::String display_name;
    container::String version;
};

using GetConfigFn = std::function<ConfigInfo()>;
using SetModelFn = std::function<void(const container::String& model)>;

struct ConfigService {
    GetConfigFn get_config;
    SetModelFn set_model;
};

} // namespace ben_gear::server
