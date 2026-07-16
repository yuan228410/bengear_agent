#pragma once

#include "server/core/router.hpp"
#include "server/api/session_types.hpp"

#include <memory>

namespace ben_gear::server {

void register_session_routes(Router& router, std::shared_ptr<SessionService> service);

} // namespace ben_gear::server
