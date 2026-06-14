#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/config/settings.hpp"
#include <string>

namespace ben_gear::server {

bool authenticate(const HttpRequest& req,
                  const config::ServerSettings& settings,
                  std::string& username);

} // namespace ben_gear::server
