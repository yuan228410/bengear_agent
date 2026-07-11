#pragma once

#include "server/core/router.hpp"
#include "server/api/git_types.hpp"

namespace ben_gear::server {

void register_git_routes(Router& router, GitApiService& service);

} // namespace ben_gear::server
