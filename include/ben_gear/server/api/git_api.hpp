#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

namespace ben_gear::server {

void register_git_routes(Router& router, GitApiService& service);

} // namespace ben_gear::server
