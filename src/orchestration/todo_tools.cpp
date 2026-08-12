#include "orchestration/todo_tools.hpp"
#include "orchestration/todo.hpp"
#include "orchestration/serializer.hpp"
#include "capabilities/tool/registry.hpp"
#include "base/core/service_registry.hpp"
#include "base/core/event_bus.hpp"
#include "agent/core/events.hpp"
#include <string>

namespace ben_gear::orchestration {

void register_todo_tools(
    capabilities::tool::ToolRegistry& registry,
    base::ServiceRegistry& services) {

    auto& svc_ref = services;

    registry.register_tool(
        std::string("update_todo"),
        std::string("STRICT WORKFLOW - follow these steps IN ORDER:\n"
                    "1. FIRST: call this tool ONCE for EACH step to create all TODOs (set status='pending')\n"
                    "2. THEN: execute each step one by one\n"
                    "3. Before each step: update its status to 'running'\n"
                    "4. After each step: update its status to 'succeeded'\n"
                    "Example for 'search tool in 3 steps': create 3 TODOs first, then execute.\n"
                    "Simple questions: skip this tool."),
        {
            {std::string("action"),
             {std::string("string"),
              std::string("Action to perform: create, update, delete, or clear"),
              {std::string("create"), std::string("update"),
               std::string("delete"), std::string("clear")}, true}},
            {std::string("todo_id"),
             {std::string("string"),
              std::string("Todo item ID (auto-generated if empty for 'create')"),
              {}, false}},
            {std::string("title"),
             {std::string("string"),
              std::string("Title/description of the todo item"),
              {}, false}},
            {std::string("status"),
             {std::string("string"),
              std::string("Status: pending, running, succeeded, failed, blocked, skipped"),
              {std::string("pending"), std::string("running"),
               std::string("succeeded"), std::string("failed"),
               std::string("blocked"), std::string("skipped")}, false}},
            {std::string("progress"),
             {std::string("integer"),
              std::string("Progress percentage (0-100)"),
              {}, false}},
            {std::string("summary"),
             {std::string("string"),
              std::string("Result summary or notes about this item"),
              {}, false}},
        },
        [&svc_ref](const Json& args) -> std::string {
            auto* todo_mgr = svc_ref.resolve<TodoManager>();
            if (!todo_mgr) {
                return std::string(R"({"error":"todo service not available"})");
            }

            auto action = args.value("action", std::string("create"));

            if (action == "clear") {
                todo_mgr->reset();
                if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                    eb->publish(agent::TodoUpdateEvent{
                        {}, {}, {}, {}, {}, std::string("clear"), 0, {}});
                }
                return to_json_string(todo_mgr->state());
            }

            if (action == "delete") {
                auto todo_id = args.value("todo_id", std::string());
                if (todo_id.empty()) {
                    return std::string(R"({"error":"todo_id required for delete"})");
                }
                auto delta = todo_mgr->remove(std::string_view(todo_id.data(), todo_id.size()));
                if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                    eb->publish(agent::TodoUpdateEvent{
                        std::move(todo_id), {}, {}, {}, {}, std::string("deleted"), 0, {}});
                }
                return to_json_string(todo_mgr->state());
            }

            TodoItem item;
            item.todo_id = args.value("todo_id", std::string());
            if (action == "create" && item.todo_id.empty()) {
                auto count = todo_mgr->state().items.size() + 1;
                item.todo_id = "todo:" + std::to_string(count);
            }
            item.title = args.value("title", std::string());
            item.progress = args.value("progress", 0);
            item.result_summary = args.value("summary", std::string());

            if (action == "create") {
                item.status = TodoStatus::pending;
            } else {
                if (item.todo_id.empty()) {
                    return std::string(R"({"error":"todo_id required for update"})");
                }
                item.status = todo_status_from_string(
                    args.value("status", std::string("pending")));
            }

            auto delta = todo_mgr->upsert(std::move(item), action);

            if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                eb->publish(agent::TodoUpdateEvent{
                    delta.item.todo_id,
                    delta.session_id,
                    delta.workspace,
                    delta.item.title,
                    std::string(to_string(delta.item.status)),
                    std::move(delta.action),
                    delta.item.progress,
                    delta.item.result_summary});
            }

            return to_json_string(todo_mgr->state());
        });
}

} // namespace ben_gear::orchestration
