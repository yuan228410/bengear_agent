#pragma once

#include "ben_gear/server/api/runtime_types.hpp"
#include "ben_gear/server/core/router.hpp"

namespace ben_gear::server {

void register_runtime_routes(Router& router, RuntimeApiService& svc);

} // namespace ben_gear::server
