#pragma once

#include "memory/store.hpp"
#include "memory/context.hpp"
#include "workspace/manager.hpp"
#include "workspace/history_db.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

/// Abstract interface for memory subsystem — enables mock injection for testing
struct IMemoryContext {
    virtual ~IMemoryContext() = default;
    virtual const std::shared_ptr<memory::MemoryStore>& store() const = 0;
    virtual const std::unique_ptr<memory::ContextBuilder>& builder() const = 0;
    virtual workspace::HistoryDB& history_db() = 0;
    virtual const std::shared_ptr<workspace::WorkspaceManager>& ws_manager() const = 0;
};

/// Concrete memory subsystem: store, context builder, history, workspace manager
struct MemoryContext : IMemoryContext {
    std::shared_ptr<memory::MemoryStore> store_;
    std::unique_ptr<memory::ContextBuilder> builder_;
    std::shared_ptr<workspace::HistoryDB> history_db_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;

    const std::shared_ptr<memory::MemoryStore>& store() const override { return store_; }
    const std::unique_ptr<memory::ContextBuilder>& builder() const override { return builder_; }
    workspace::HistoryDB& history_db() override { return *history_db_; }
    const std::shared_ptr<workspace::WorkspaceManager>& ws_manager() const override { return ws_manager_; }
};

} // namespace ben_gear::agent::runtime
