#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/orchestration/plan.hpp"
#include "ben_gear/orchestration/store.hpp"
#include "ben_gear/orchestration/todo.hpp"

namespace ben_gear::orchestration {

Json to_json(const ExecutionValue& value);
Json to_json(const ExecutionResult& result);
Json to_json(const ExecutionEvent& event);
Json to_json(const ExecutionSnapshot& snapshot);
Json to_json(const ExecutionStoreSnapshot& snapshot);
Json to_json(const PlanItemChoice& choice);
Json to_json(const PlanItem& item);
Json to_json(const PlanOption& option);
Json to_json(const PlanDraft& draft);
Json to_json(const TodoItem& item);
Json to_json(const TodoState& state);
Json to_json(const TodoDelta& delta);
PlanItemChoice plan_item_choice_from_json(const Json& json);
PlanItem plan_item_from_json(const Json& json);
PlanOption plan_option_from_json(const Json& json);
PlanDraft plan_draft_from_json(const Json& json);
TodoItem todo_item_from_json(const Json& json);
TodoState todo_state_from_json(const Json& json);

container::String to_json_string(const ExecutionValue& value);
container::String to_json_string(const ExecutionResult& result);
container::String to_json_string(const ExecutionEvent& event);
container::String to_json_string(const ExecutionSnapshot& snapshot);
container::String to_json_string(const ExecutionStoreSnapshot& snapshot);
container::String to_json_string(const PlanDraft& draft);
container::String to_json_string(const TodoState& state);
container::String to_json_string(const TodoDelta& delta);

} // namespace ben_gear::orchestration
