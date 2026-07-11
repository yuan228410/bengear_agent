#pragma once

#include "server/core/router.hpp"
#include "server/api/session_types.hpp"

namespace ben_gear::server {

void register_session_routes(Router& router, SessionService& service);

} // namespace ben_gear::server
