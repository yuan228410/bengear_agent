#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

namespace ben_gear::server {

void register_session_routes(Router& router, SessionService& service);

} // namespace ben_gear::server
