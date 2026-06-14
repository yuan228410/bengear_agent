#pragma once

#include "ben_gear/orchestration/plan.hpp"

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
    container::String todo_id;
    container::String session_id;
    container::String workspace;
    container::String title;
    container::String active_form;
    container::String source_plan_item_id;
    container::String parent_id;
    container::String result_summary;
    TodoStatus status = TodoStatus::pending;
    int order = 0;
    int progress = 0;
    uint64_t updated_ms = 0;
    bool has_status = true;
    bool has_progress = true;
};

struct TodoState {
    container::String session_id;
    container::String workspace;
    container::String plan_id;
    container::Vector<TodoItem> items;
    uint64_t updated_ms = 0;
};

struct TodoDelta {
    container::String session_id;
    container::String workspace;
    container::String plan_id;
    TodoItem item;
    container::String action;
};

const char* to_string(TodoStatus status);
TodoStatus todo_status_from_string(std::string_view value);

class TodoManager {
public:
    const TodoState& state() const noexcept { return state_; }
    bool empty() const noexcept { return state_.items.empty(); }

    const TodoState& initialize_from_plan(const PlanDraft& plan);
    TodoDelta upsert(TodoItem item, container::String action = container::String("updated"));
    TodoDelta update_status(container::String todo_id,
                            TodoStatus status,
                            container::String summary = {},
                            int progress = -1);
    const TodoState& restore(TodoState state);
    void mark_all_running_as(TodoStatus status, container::String summary);
    void mark_running_as(TodoStatus status, container::String summary);
    void reset(container::String session_id = {}, container::String workspace = {});

private:
    TodoItem* find(std::string_view todo_id);
    void touch();

    TodoState state_;
};

} // namespace ben_gear::orchestration
