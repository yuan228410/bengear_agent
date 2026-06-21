#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/audit_types.hpp"

namespace ben_gear::server {

void register_audit_routes(Router& router, AuditApiService& svc);

} // namespace ben_gear::server
