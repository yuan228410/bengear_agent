#include "orchestration/serializer.hpp"

#include <string_view>

namespace ben_gear::orchestration {

namespace {

Json metadata_to_json(const Metadata& metadata) {
    Json obj = Json::object();
    for (const auto& [key, value] : metadata) {
        obj[std::string_view(key.data(), key.size())] = value;
    }
    return obj;
}

Json usage_to_json(const llm::TokenUsage& usage) {
    return Json{{"prompt_tokens", usage.prompt_tokens},
                {"completion_tokens", usage.completion_tokens},
                {"total_tokens", usage.total_tokens},
                {"cached_tokens", usage.cached_tokens}};
}

Json latency_to_json(const llm::RequestLatency& latency) {
    return Json{{"total_seconds", latency.total_seconds},
                {"ttfb_seconds", latency.ttfb_seconds},
                {"has_ttfb", latency.has_ttfb}};
}

Json child_summary_to_json(const ExecutionChildSummary& child) {
    return Json{{"execution_id", child.execution_id},
                {"kind", to_string(child.kind)},
                {"status", to_string(child.status)},
                {"output", to_json(child.output)},
                {"error", child.error}};
}

Json result_children_to_json(const std::vector<ExecutionChildSummary>& children) {
    Json array = Json::array();
    for (const auto& child : children) array.push_back(child_summary_to_json(child));
    return array;
}

Json snapshots_to_json(const std::vector<ExecutionSnapshot>& snapshots) {
    Json array = Json::array();
    for (const auto& snapshot : snapshots) array.push_back(to_json(snapshot));
    return array;
}

Json strings_to_json(const std::vector<std::string>& values) {
    Json array = Json::array();
    for (const auto& value : values) array.push_back(value);
    return array;
}

std::vector<std::string> strings_from_json(const Json& json) {
    std::vector<std::string> values;
    if (!json.is_array()) return values;
    for (size_t i = 0; i < json.size(); ++i) {
        if (json[i].is_string()) values.push_back(json[i].as_string());
    }
    return values;
}

Json plan_item_choices_to_json(const std::vector<PlanItemChoice>& choices) {
    Json array = Json::array();
    for (const auto& choice : choices) array.push_back(to_json(choice));
    return array;
}

Json plan_decisions_to_json(const std::vector<PlanDecision>& decisions) {
    Json array = Json::array();
    for (const auto& decision : decisions) array.push_back(to_json(decision));
    return array;
}

Json plan_items_to_json(const std::vector<PlanItem>& items) {
    Json array = Json::array();
    for (const auto& item : items) array.push_back(to_json(item));
    return array;
}

Json plan_options_to_json(const std::vector<PlanOption>& options) {
    Json array = Json::array();
    for (const auto& option : options) array.push_back(to_json(option));
    return array;
}

Json todo_items_to_json(const std::vector<TodoItem>& items) {
    Json array = Json::array();
    for (const auto& item : items) array.push_back(to_json(item));
    return array;
}

std::string dump_to_string(const Json& json) {
    return json.dump();
}

} // namespace

Json to_json(const ExecutionValue& value) {
    return Json{{"text", value.text_view()}, {"fields", metadata_to_json(value.fields_view())}};
}

Json to_json(const ExecutionResult& result) {
    return Json{{"execution_id", result.execution_id},
                {"parent_id", result.parent_id},
                {"kind", to_string(result.kind)},
                {"status", to_string(result.status)},
                {"success", result.success()},
                {"output", to_json(result.output)},
                {"error", result.error},
                {"usage", usage_to_json(result.usage)},
                {"latency", latency_to_json(result.latency)},
                {"metrics", metadata_to_json(result.metrics)},
                {"children", result_children_to_json(result.children)}};
}

Json to_json(const ExecutionEvent& event) {
    return Json{{"execution_id", event.execution_id},
                {"parent_id", event.parent_id},
                {"trace_id", event.trace_id},
                {"kind", to_string(event.kind)},
                {"type", to_string(event.type)},
                {"status", to_string(event.status)},
                {"message", event.message},
                {"payload", to_json(event.payload)},
                {"usage", usage_to_json(event.usage)},
                {"latency", latency_to_json(event.latency)},
                {"timestamp_ms", static_cast<int64_t>(event.timestamp_ms)},
                {"sequence", static_cast<int64_t>(event.sequence)}};
}

Json to_json(const ExecutionSnapshot& snapshot) {
    return Json{{"execution_id", snapshot.execution_id},
                {"parent_id", snapshot.parent_id},
                {"trace_id", snapshot.trace_id},
                {"kind", to_string(snapshot.kind)},
                {"status", to_string(snapshot.status)},
                {"last_error", snapshot.last_error},
                {"result", to_json(snapshot.result)}};
}

Json to_json(const ExecutionStoreSnapshot& snapshot) {
    return Json{{"running_count", static_cast<int64_t>(snapshot.running_count)},
                {"completed_count", static_cast<int64_t>(snapshot.completed_count)},
                {"failed_count", static_cast<int64_t>(snapshot.failed_count)},
                {"cancelled_count", static_cast<int64_t>(snapshot.cancelled_count)},
                {"timeout_count", static_cast<int64_t>(snapshot.timeout_count)},
                {"active", snapshots_to_json(snapshot.active)},
                {"completed", snapshots_to_json(snapshot.completed)}};
}

Json to_json(const PlanItemChoice& choice) {
    return Json{{"id", choice.id},
                {"title", choice.title},
                {"description", choice.description},
                {"recommended", choice.recommended}};
}

Json to_json(const PlanDecision& decision) {
    return Json{{"id", decision.id},
                {"title", decision.title},
                {"description", decision.description},
                {"required", decision.required},
                {"choices", plan_item_choices_to_json(decision.choices)},
                {"selected_choice_id", decision.selected_choice_id},
                {"custom_note", decision.custom_note}};
}

Json to_json(const PlanItem& item) {
    return Json{{"id", item.id},
                {"title", item.title},
                {"description", item.description},
                {"order", item.order},
                {"required", item.required},
                {"choices", plan_item_choices_to_json(item.choices)},
                {"selected_choice_id", item.selected_choice_id},
                {"custom_note", item.custom_note},
                {"decisions", plan_decisions_to_json(item.decisions)},
                {"risks", strings_to_json(item.risks)},
                {"validation", strings_to_json(item.validation)}};
}

Json to_json(const PlanOption& option) {
    return Json{{"id", option.id},
                {"title", option.title},
                {"summary", option.summary},
                {"items", plan_items_to_json(option.items)},
                {"recommended", option.recommended}};
}

Json to_json(const PlanDraft& draft) {
    return Json{{"plan_id", draft.plan_id},
                {"session_id", draft.session_id},
                {"workspace", draft.workspace},
                {"title", draft.title},
                {"objective", draft.objective},
                {"status", to_string(draft.status)},
                {"stage", to_string(draft.stage)},
                {"revision", draft.revision},
                {"options", plan_options_to_json(draft.options)},
                {"selected_option_id", draft.selected_option_id},
                {"detailed_option_id", draft.detailed_option_id},
                {"items", plan_items_to_json(draft.items)},
                {"global_risks", strings_to_json(draft.global_risks)},
                {"validation", strings_to_json(draft.validation)},
                {"final_summary", draft.final_summary},
                {"final_items", plan_items_to_json(draft.final_items)},
                {"consistency_notes", strings_to_json(draft.consistency_notes)},
                {"finalized_input_revision", draft.finalized_input_revision},
                {"planning_request_id", static_cast<int64_t>(draft.planning_request_id)},
                {"error", draft.error},
                {"updated_ms", static_cast<int64_t>(draft.updated_ms)}};
}

Json to_json(const TodoItem& item) {
    return Json{{"todo_id", item.todo_id},
                {"session_id", item.session_id},
                {"workspace", item.workspace},
                {"title", item.title},
                {"active_form", item.active_form},
                {"source_plan_item_id", item.source_plan_item_id},
                {"parent_id", item.parent_id},
                {"result_summary", item.result_summary},
                {"status", to_string(item.status)},
                {"order", item.order},
                {"progress", item.progress},
                {"updated_ms", static_cast<int64_t>(item.updated_ms)}};
}

Json to_json(const TodoState& state) {
    return Json{{"session_id", state.session_id},
                {"workspace", state.workspace},
                {"plan_id", state.plan_id},
                {"items", todo_items_to_json(state.items)},
                {"updated_ms", static_cast<int64_t>(state.updated_ms)}};
}

Json to_json(const TodoDelta& delta) {
    return Json{{"session_id", delta.session_id},
                {"workspace", delta.workspace},
                {"plan_id", delta.plan_id},
                {"action", delta.action},
                {"item", to_json(delta.item)}};
}

PlanItemChoice plan_item_choice_from_json(const Json& json) {
    PlanItemChoice choice;
    choice.id = json.value("id", "");
    choice.title = json.value("title", "");
    choice.description = json.value("description", "");
    choice.recommended = json.value("recommended", false);
    return choice;
}

PlanDecision plan_decision_from_json(const Json& json) {
    PlanDecision decision;
    decision.id = json.value("id", "");
    decision.title = json.value("title", "");
    decision.description = json.value("description", "");
    decision.required = json.value("required", true);
    decision.selected_choice_id = json.value("selected_choice_id", "");
    decision.custom_note = json.value("custom_note", "");
    auto choices = json["choices"];
    if (choices.is_array()) {
        for (size_t i = 0; i < choices.size(); ++i) decision.choices.push_back(plan_item_choice_from_json(choices[i]));
    }
    return decision;
}

PlanItem plan_item_from_json(const Json& json) {
    PlanItem item;
    item.id = json.value("id", "");
    item.title = json.value("title", "");
    item.description = json.value("description", "");
    item.order = json.value("order", 0);
    item.required = json.value("required", true);
    item.selected_choice_id = json.value("selected_choice_id", "");
    item.custom_note = json.value("custom_note", "");
    auto choices = json["choices"];
    if (choices.is_array()) {
        for (size_t i = 0; i < choices.size(); ++i) item.choices.push_back(plan_item_choice_from_json(choices[i]));
    }
    auto decisions = json["decisions"];
    if (decisions.is_array()) {
        for (size_t i = 0; i < decisions.size(); ++i) item.decisions.push_back(plan_decision_from_json(decisions[i]));
    }
    item.risks = strings_from_json(json["risks"]);
    item.validation = strings_from_json(json["validation"]);
    return item;
}

PlanOption plan_option_from_json(const Json& json) {
    PlanOption option;
    option.id = json.value("id", "");
    option.title = json.value("title", "");
    option.summary = json.value("summary", "");
    option.recommended = json.value("recommended", false);
    auto items = json["items"];
    if (items.is_array()) {
        for (size_t i = 0; i < items.size(); ++i) option.items.push_back(plan_item_from_json(items[i]));
    }
    return option;
}

PlanDraft plan_draft_from_json(const Json& json) {
    PlanDraft draft;
    draft.plan_id = json.value("plan_id", "");
    draft.session_id = json.value("session_id", "");
    draft.workspace = json.value("workspace", "");
    draft.title = json.value("title", "");
    draft.objective = json.value("objective", "");
    draft.status = plan_status_from_string(json.value("status", "idle"));
    draft.stage = plan_stage_from_string(json.value("stage", "idle"));
    draft.revision = json.value("revision", 0);
    draft.selected_option_id = json.value("selected_option_id", "");
    draft.detailed_option_id = json.value("detailed_option_id", "");
    draft.error = json.value("error", "");
    draft.final_summary = json.value("final_summary", "");
    draft.finalized_input_revision = json.value("finalized_input_revision", 0);
    draft.planning_request_id = static_cast<uint64_t>(json.value("planning_request_id", static_cast<int64_t>(0)));
    draft.updated_ms = static_cast<uint64_t>(json.value("updated_ms", static_cast<int64_t>(0)));
    auto options = json["options"];
    if (options.is_array()) {
        for (size_t i = 0; i < options.size(); ++i) draft.options.push_back(plan_option_from_json(options[i]));
    }
    auto items = json["items"];
    if (items.is_array()) {
        for (size_t i = 0; i < items.size(); ++i) draft.items.push_back(plan_item_from_json(items[i]));
    }
    auto final_items = json["final_items"];
    if (final_items.is_array()) {
        for (size_t i = 0; i < final_items.size(); ++i) draft.final_items.push_back(plan_item_from_json(final_items[i]));
    }
    draft.global_risks = strings_from_json(json["global_risks"]);
    draft.validation = strings_from_json(json["validation"]);
    draft.consistency_notes = strings_from_json(json["consistency_notes"]);
    return draft;
}

TodoItem todo_item_from_json(const Json& json) {
    TodoItem item;
    item.todo_id = json.value("todo_id", "");
    item.session_id = json.value("session_id", "");
    item.workspace = json.value("workspace", "");
    item.title = json.value("title", "");
    item.active_form = json.value("active_form", "");
    item.source_plan_item_id = json.value("source_plan_item_id", "");
    item.parent_id = json.value("parent_id", "");
    item.result_summary = json.value("result_summary", "");
    item.status = todo_status_from_string(json.value("status", "pending"));
    item.order = json.value("order", 0);
    item.progress = json.value("progress", 0);
    item.updated_ms = static_cast<uint64_t>(json.value("updated_ms", static_cast<int64_t>(0)));
    return item;
}

TodoState todo_state_from_json(const Json& json) {
    TodoState state;
    state.session_id = json.value("session_id", "");
    state.workspace = json.value("workspace", "");
    state.plan_id = json.value("plan_id", "");
    state.updated_ms = static_cast<uint64_t>(json.value("updated_ms", static_cast<int64_t>(0)));
    auto items = json["items"];
    if (items.is_array()) {
        for (size_t i = 0; i < items.size(); ++i) state.items.push_back(todo_item_from_json(items[i]));
    }
    return state;
}

std::string to_json_string(const ExecutionValue& value) { return dump_to_string(to_json(value)); }
std::string to_json_string(const ExecutionResult& result) { return dump_to_string(to_json(result)); }
std::string to_json_string(const ExecutionEvent& event) { return dump_to_string(to_json(event)); }
std::string to_json_string(const ExecutionSnapshot& snapshot) { return dump_to_string(to_json(snapshot)); }
std::string to_json_string(const ExecutionStoreSnapshot& snapshot) { return dump_to_string(to_json(snapshot)); }
std::string to_json_string(const PlanDraft& draft) { return dump_to_string(to_json(draft)); }
std::string to_json_string(const TodoState& state) { return dump_to_string(to_json(state)); }
std::string to_json_string(const TodoDelta& delta) { return dump_to_string(to_json(delta)); }

} // namespace ben_gear::orchestration
