#pragma once

#include "server/core/router.hpp"
#include "server/api/audit_types.hpp"

namespace ben_gear::server {

void register_audit_routes(Router& router, AuditApiService& svc);

} // namespace ben_gear::server
