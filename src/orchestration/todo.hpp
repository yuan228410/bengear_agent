#pragma once

#include "orchestration/plan.hpp"

namespace ben_gear::orchestration {

/// 执行 TODO 状态：结构化数据，不绑定 UI 展示。
enum class TodoStatus {
    pending,
    running,
    succeeded,
    failed,
    cancelled,
    blocked,
    skipped,
};

struct TodoItem {
    std::string todo_id;
    std::string session_id;
    std::string workspace;
    std::string title;
    std::string active_form;
    std::string source_plan_item_id;
    std::string parent_id;
    std::string result_summary;
    TodoStatus status = TodoStatus::pending;
    int order = 0;
    int progress = 0;
    uint64_t updated_ms = 0;
    bool has_status = true;
    bool has_progress = true;
};

struct TodoState {
    std::string session_id;
    std::string workspace;
    std::string plan_id;
    std::vector<TodoItem> items;
    uint64_t updated_ms = 0;
};

struct TodoDelta {
    std::string session_id;
    std::string workspace;
    std::string plan_id;
    TodoItem item;
    std::string action;
};

const char* to_string(TodoStatus status);
TodoStatus todo_status_from_string(std::string_view value);

class TodoManager {
public:
    const TodoState& state() const noexcept { return state_; }
    bool empty() const noexcept { return state_.items.empty(); }

    const TodoState& initialize_from_plan(const PlanDraft& plan);
    TodoDelta upsert(TodoItem item, std::string action = std::string("updated"));
    TodoDelta update_status(std::string todo_id,
                            TodoStatus status,
                            std::string summary = {},
                            int progress = -1);
    const TodoState& restore(TodoState state);
    void mark_all_running_as(TodoStatus status, std::string summary);
    void mark_running_as(TodoStatus status, std::string summary);
    void reset(std::string session_id = {}, std::string workspace = {});

private:
    TodoItem* find(std::string_view todo_id);
    void touch();

    TodoState state_;
};

} // namespace ben_gear::orchestration
