#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

namespace ben_gear::server {

void register_test_loop_routes(Router& router, TestLoopApiService& svc);

} // namespace ben_gear::server
