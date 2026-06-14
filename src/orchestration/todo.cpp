#include "ben_gear/orchestration/todo.hpp"

#include <string>
#include <utility>

namespace ben_gear::orchestration {

namespace {

container::String todo_id_for(const PlanItem& item, int order) {
    container::String id("todo:");
    if (!item.id.empty()) {
        id.append(item.id);
    } else {
        auto suffix = std::to_string(order);
        id.append(std::string_view(suffix.data(), suffix.size()));
    }
    return id;
}

} // namespace

const char* to_string(TodoStatus status) {
    switch (status) {
    case TodoStatus::pending: return "pending";
    case TodoStatus::running: return "running";
    case TodoStatus::succeeded: return "succeeded";
    case TodoStatus::failed: return "failed";
    case TodoStatus::cancelled: return "cancelled";
    case TodoStatus::blocked: return "blocked";
    case TodoStatus::skipped: return "skipped";
    }
    return "failed";
}

TodoStatus todo_status_from_string(std::string_view value) {
    if (value == "pending") return TodoStatus::pending;
    if (value == "running") return TodoStatus::running;
    if (value == "succeeded") return TodoStatus::succeeded;
    if (value == "cancelled") return TodoStatus::cancelled;
    if (value == "blocked") return TodoStatus::blocked;
    if (value == "skipped") return TodoStatus::skipped;
    return TodoStatus::failed;
}

const TodoState& TodoManager::initialize_from_plan(const PlanDraft& plan) {
    state_ = {};
    state_.session_id = plan.session_id;
    state_.workspace = plan.workspace;
    state_.plan_id = plan.plan_id;
    int order = 1;
    for (const auto& plan_item : plan.items) {
        TodoItem item;
        item.todo_id = todo_id_for(plan_item, order);
        item.session_id = plan.session_id;
        item.workspace = plan.workspace;
        item.title = plan_item.title;
        item.active_form = plan_item.title;
        item.source_plan_item_id = plan_item.id;
        item.status = TodoStatus::pending;
        item.order = order++;
        item.updated_ms = now_ms();
        state_.items.push_back(std::move(item));
    }
    touch();
    return state_;
}

TodoDelta TodoManager::upsert(TodoItem item, container::String action) {
    item.updated_ms = now_ms();
    if (item.session_id.empty()) item.session_id = state_.session_id;
    if (item.workspace.empty()) item.workspace = state_.workspace;
    if (auto* existing = find(std::string_view(item.todo_id.data(), item.todo_id.size()))) {
        if (!item.session_id.empty()) existing->session_id = item.session_id;
        if (!item.workspace.empty()) existing->workspace = item.workspace;
        if (!item.title.empty()) existing->title = item.title;
        if (!item.active_form.empty()) existing->active_form = item.active_form;
        if (!item.source_plan_item_id.empty()) existing->source_plan_item_id = item.source_plan_item_id;
        if (!item.parent_id.empty()) existing->parent_id = item.parent_id;
        if (!item.result_summary.empty()) existing->result_summary = item.result_summary;
        if (item.has_status) existing->status = item.status;
        if (item.order > 0) existing->order = item.order;
        if (item.has_progress && item.progress >= 0) existing->progress = item.progress;
        existing->updated_ms = item.updated_ms;
        item = *existing;
    } else {
        item.order = item.order > 0 ? item.order : static_cast<int>(state_.items.size()) + 1;
        state_.items.push_back(item);
    }
    touch();
    return TodoDelta{state_.session_id, state_.workspace, state_.plan_id, std::move(item), std::move(action)};
}

TodoDelta TodoManager::update_status(container::String todo_id,
                                     TodoStatus status,
                                     container::String summary,
                                     int progress) {
    TodoItem item;
    if (auto* existing = find(std::string_view(todo_id.data(), todo_id.size()))) {
        existing->status = status;
        if (!summary.empty()) existing->result_summary = summary;
        if (progress >= 0) existing->progress = progress;
        existing->updated_ms = now_ms();
        item = *existing;
    } else {
        item.todo_id = std::move(todo_id);
        item.session_id = state_.session_id;
        item.workspace = state_.workspace;
        item.title = item.todo_id;
        item.status = status;
        item.result_summary = std::move(summary);
        item.progress = progress >= 0 ? progress : 0;
        item.order = static_cast<int>(state_.items.size()) + 1;
        item.updated_ms = now_ms();
        state_.items.push_back(item);
    }
    touch();
    return TodoDelta{state_.session_id, state_.workspace, state_.plan_id, item, container::String("status")};
}

const TodoState& TodoManager::restore(TodoState state) {
    state_ = std::move(state);
    if (state_.updated_ms == 0) touch();
    return state_;
}

void TodoManager::mark_all_running_as(TodoStatus status, container::String summary) {
    for (auto& item : state_.items) {
        if (item.status == TodoStatus::running || item.status == TodoStatus::pending) {
            item.status = status;
            item.result_summary = summary;
            item.updated_ms = now_ms();
        }
    }
    touch();
}

void TodoManager::mark_running_as(TodoStatus status, container::String summary) {
    for (auto& item : state_.items) {
        if (item.status == TodoStatus::running) {
            item.status = status;
            item.result_summary = summary;
            item.updated_ms = now_ms();
        }
    }
    touch();
}

void TodoManager::reset(container::String session_id, container::String workspace) {
    state_ = {};
    state_.session_id = std::move(session_id);
    state_.workspace = std::move(workspace);
    touch();
}

TodoItem* TodoManager::find(std::string_view todo_id) {
    for (auto& item : state_.items) {
        if (std::string_view(item.todo_id.data(), item.todo_id.size()) == todo_id) return &item;
    }
    return nullptr;
}

void TodoManager::touch() {
    state_.updated_ms = now_ms();
}

} // namespace ben_gear::orchestration
