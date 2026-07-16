#pragma once

#include "server/api/common.hpp"

#include <memory>
#include <string>

namespace ben_gear::server {

struct ConfigInfo {
    std::string model;
    std::string provider;
    std::string workspace;
    std::string display_name;
    std::string version;
};

class ConfigService {
public:
    virtual ~ConfigService() = default;

    virtual ConfigInfo get_config() = 0;
    virtual void set_model(const std::string& model) = 0;
};

} // namespace ben_gear::server
