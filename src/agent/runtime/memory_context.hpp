#pragma once

#include "memory/store.hpp"
#include "memory/context.hpp"
#include "workspace/manager.hpp"
#include "workspace/history_db.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

struct MemoryContext {
    std::shared_ptr<memory::MemoryStore> store;
    std::unique_ptr<memory::ContextBuilder> builder;
    std::unique_ptr<workspace::HistoryDB> history_db;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager;
};

} // namespace ben_gear::agent::runtime
