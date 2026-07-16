#pragma once

#include "workspace/types.hpp"

#include <memory>

namespace ben_gear::server::composition {

class WorkspaceApplicationServices {
public:
    explicit WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx);

    const workspace::WorkspaceContext& workspace_context() const { return ws_ctx_; }

private:
    workspace::WorkspaceContext ws_ctx_;
};

} // namespace ben_gear::server::composition
