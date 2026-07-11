#pragma once

#include "server/api/runtime_types.hpp"
#include "server/core/router.hpp"

namespace ben_gear::server {

void register_runtime_routes(Router& router, RuntimeApiService& svc);

} // namespace ben_gear::server
