#pragma once

#include "server/core/router.hpp"
#include "server/api/repo_map_types.hpp"

namespace ben_gear::server {

void register_repo_map_routes(Router& router, RepoMapApiService& svc);

} // namespace ben_gear::server
