#pragma once

#include "server/core/router.hpp"
#include "server/api/permission_types.hpp"

namespace ben_gear::server {

void register_permission_routes(Router& router, PermissionApiService& service);

} // namespace ben_gear::server
