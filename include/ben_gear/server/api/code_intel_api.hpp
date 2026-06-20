#pragma once

#include "ben_gear/server/api/deps.hpp"
#include "ben_gear/server/core/router.hpp"

namespace ben_gear::server {

void register_code_intel_routes(Router& router, CodeIntelApiService& svc);

} // namespace ben_gear::server
