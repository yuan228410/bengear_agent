#include "ben_gear/orchestration/plan_parser.hpp"

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/orchestration/serializer.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ben_gear::orchestration {

namespace {

std::string to_std(std::string_view value) {
    return std::string(value.data(), value.size());
}

std::string compact_output(const container::String& output) {
    std::string text(output.data(), output.size());
    if (text.size() > 1600) text.resize(1600);
    return text;
}

std::string extract_json_text(std::string_view text) {
    auto fenced = text.find("```json");
    if (fenced == std::string_view::npos) fenced = text.find("```");
    if (fenced != std::string_view::npos) {
        auto start = text.find('\n', fenced);
        if (start != std::string_view::npos) {
            ++start;
            auto end = text.find("```", start);
            if (end != std::string_view::npos) return to_std(text.substr(start, end - start));
        }
    }
    auto object_start = text.find('{');
    auto array_start = text.find('[');
    auto start = std::min(object_start, array_start);
    if (start == std::string_view::npos) start = std::max(object_start, array_start);
    if (start == std::string_view::npos) return to_std(text);
    char open = text[start];
    char close = open == '{' ? '}' : ']';
    int depth = 0;
    bool in_string = false;
    bool escaping = false;
    for (size_t i = start; i < text.size(); ++i) {
        char ch = text[i];
        if (escaping) { escaping = false; continue; }
        if (ch == '\\' && in_string) { escaping = true; continue; }
        if (ch == '"') { in_string = !in_string; continue; }
        if (in_string) continue;
        if (ch == open) ++depth;
        if (ch == close) {
            --depth;
            if (depth == 0) return to_std(text.substr(start, i - start + 1));
        }
    }
    return to_std(text.substr(start));
}

Json parse_object(std::string_view text, PlanParseResult& result) {
    auto json_text = extract_json_text(text);
    std::string error;
    auto json = parse_json(json_text, error);
    if (!error.empty() || !json.is_object()) {
        result.error = error.empty() ? container::String("LLM output is not a JSON object") : container::String(error.c_str());
        return Json();
    }
    return json;
}

bool valid_options(const PlanDraft& draft, container::String& error) {
    if (draft.options.empty()) {
        error = "plan must contain at least one option";
        return false;
    }
    for (const auto& option : draft.options) {
        if (option.title.empty() && option.summary.empty()) {
            error = "each plan option must contain title or summary";
            return false;
        }
    }
    return true;
}

bool valid_items(const container::Vector<PlanItem>& items, container::String& error) {
    if (items.empty()) {
        error = "plan must contain at least one item";
        return false;
    }
    for (const auto& item : items) {
        if (item.title.empty() && item.description.empty()) {
            error = "each plan item must contain title or description";
            return false;
        }
        for (const auto& decision : item.decisions) {
            if (decision.title.empty() && decision.description.empty()) {
                error = "each plan decision must contain title or description";
                return false;
            }
            if (decision.required && decision.choices.empty() && decision.custom_note.empty()) {
                error = "required decisions must contain choices or custom_note";
                return false;
            }
        }
    }
    return true;
}

void append_line(std::string& text, std::string_view line) {
    text.append(line.data(), line.size());
    text.push_back('\n');
}

void append_json_line(std::string& text, const PlanDraft& draft) {
    auto json = to_json_string(draft);
    text.append(json.data(), json.size());
    text.push_back('\n');
}

} // namespace

PlanParseResult parse_plan_options_text(std::string_view text,
                                        const container::String& session_id,
                                        const container::String& workspace,
                                        const container::String& fallback_objective) {
    PlanParseResult result;
    auto json = parse_object(text, result);
    if (!result.error.empty()) return result;

    auto parsed = plan_draft_from_json(json);
    parsed.session_id = session_id;
    parsed.workspace = workspace;
    if (parsed.objective.empty()) parsed.objective = fallback_objective;
    if (parsed.title.empty()) parsed.title = "Plan options";

    container::String validation_error;
    if (!valid_options(parsed, validation_error)) {
        result.error = validation_error;
        return result;
    }

    PlanManager manager;
    PlanCommand command;
    command.session_id = session_id;
    command.workspace = workspace;
    command.prompt = parsed.objective;
    manager.start(command);
    manager.apply_model_options(std::move(parsed.title), std::move(parsed.objective), std::move(parsed.options), std::move(parsed.selected_option_id));
    result.draft = manager.draft();
    result.ok = true;
    return result;
}

PlanParseResult parse_plan_detail_text(std::string_view text,
                                       const container::String& session_id,
                                       const container::String& workspace,
                                       const container::String& fallback_objective,
                                       const container::String& selected_option_id) {
    PlanParseResult result;
    auto json = parse_object(text, result);
    if (!result.error.empty()) return result;

    auto parsed = plan_draft_from_json(json);
    parsed.session_id = session_id;
    parsed.workspace = workspace;
    if (parsed.objective.empty()) parsed.objective = fallback_objective;
    if (parsed.title.empty()) parsed.title = "Detailed plan";
    if (parsed.selected_option_id.empty()) parsed.selected_option_id = selected_option_id;

    container::String validation_error;
    if (!valid_items(parsed.items, validation_error)) {
        result.error = validation_error;
        return result;
    }
    parsed.stage = PlanStage::decision_review;
    parsed.status = PlanStatus::reviewing;
    result.draft = std::move(parsed);
    result.ok = true;
    return result;
}

PlanParseResult parse_plan_final_text(std::string_view text, const PlanDraft& source_draft) {
    PlanParseResult result;
    auto json = parse_object(text, result);
    if (!result.error.empty()) return result;

    PlanDraft parsed = source_draft;
    parsed.final_summary = json.value("final_summary", "");
    parsed.global_risks = plan_draft_from_json(json).global_risks;
    parsed.validation = plan_draft_from_json(json).validation;
    parsed.consistency_notes = plan_draft_from_json(json).consistency_notes;
    auto final_items = json["final_items"];
    parsed.final_items = {};
    if (final_items.is_array()) {
        for (size_t i = 0; i < final_items.size(); ++i) parsed.final_items.push_back(plan_item_from_json(final_items[i]));
    }
    if (parsed.final_items.empty()) {
        auto items = json["items"];
        if (items.is_array()) {
            for (size_t i = 0; i < items.size(); ++i) parsed.final_items.push_back(plan_item_from_json(items[i]));
        }
    }

    container::String validation_error;
    if (!valid_items(parsed.final_items, validation_error)) {
        result.error = validation_error;
        return result;
    }
    parsed.stage = PlanStage::final_review;
    parsed.status = PlanStatus::reviewing;
    result.draft = std::move(parsed);
    result.ok = true;
    return result;
}

PlanParseResult parse_plan_draft_text(std::string_view text,
                                      const container::String& session_id,
                                      const container::String& workspace,
                                      const container::String& fallback_objective) {
    return parse_plan_options_text(text, session_id, workspace, fallback_objective);
}

container::String build_plan_options_prompt(const container::String& objective,
                                            const container::String& user_note,
                                            const container::String& previous_error,
                                            const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Create candidate high-level implementation approaches for the user's objective.");
    append_line(prompt, "Return JSON only; no markdown or prose. This is the slow planning node for top-level options only.");
    append_line(prompt, R"(Schema: {"title":"...","objective":"...","options":[{"id":"option_1","title":"...","summary":"... include tradeoffs/when-to-use concisely ...","recommended":true,"items":[]}]})");
    append_line(prompt, "Do not generate detailed steps yet. Provide 2-4 distinct overall approaches when meaningful; otherwise one strong approach is fine.");
    append_line(prompt, "Keep titles under 10 words and summaries under 45 words. Prefer concise, actionable options to reduce planning latency.");
    append_line(prompt, "Objective:");
    append_line(prompt, std::string_view(objective.data(), objective.size()));
    if (!user_note.empty()) {
        append_line(prompt, "User note:");
        append_line(prompt, std::string_view(user_note.data(), user_note.size()));
    }
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_detail_prompt(const PlanDraft& draft,
                                           const container::String& selected_option_id,
                                           const container::String& previous_error,
                                           const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Generate the complete detailed plan for the selected high-level option.");
    append_line(prompt, "Return JSON only; no markdown or prose. This is the only slow node after choosing a top-level option.");
    append_line(prompt, R"(Schema: {"title":"...","objective":"...","selected_option_id":"option_1","items":[{"id":"step_1","title":"...","description":"...","required":true,"risks":["..."],"validation":["..."],"decisions":[{"id":"decision_1","title":"...","description":"...","required":true,"choices":[{"id":"choice_1","title":"...","description":"...","recommended":true}],"selected_choice_id":"","custom_note":""}]}],"global_risks":["..."],"validation":["..."]})");
    append_line(prompt, "Latency budget: generate a compact interaction skeleton, not exhaustive documentation.");
    append_line(prompt, "Limits: 3-5 items; each title under 10 words; each description under 35 words; at most 1 risk and 1 validation per item; global_risks and validation at most 2 entries each.");
    append_line(prompt, "Only create decisions when user input materially affects the implementation. At most 3 required decisions total, each with 2 choices. Choice descriptions under 25 words.");
    append_line(prompt, "Do not preselect decisions. recommended only hints the UI; selected_choice_id must be empty unless the user already decided.");
    append_line(prompt, "Current plan/options JSON:");
    append_json_line(prompt, draft);
    append_line(prompt, "Selected option id:");
    append_line(prompt, std::string_view(selected_option_id.data(), selected_option_id.size()));
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_finalization_prompt(const PlanDraft& draft,
                                                 const container::String& previous_error,
                                                 const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Finalize and consistency-check the user's selected plan.");
    append_line(prompt, "Return JSON only; no markdown or prose. Do not ask new decisions and do not overwrite user choices.");
    append_line(prompt, R"(Schema: {"final_summary":"...","final_items":[{"id":"step_1","title":"...","description":"... include selected decisions ...","required":true,"risks":["..."],"validation":["..."]}],"global_risks":["..."],"validation":["..."],"consistency_notes":["..."]})");
    append_line(prompt, "If choices conflict, record it in consistency_notes/risks; do not introduce new unresolved decisions.");
    append_line(prompt, "Plan with user choices JSON:");
    append_json_line(prompt, draft);
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_options_revision_prompt(const PlanDraft& draft,
                                                     const container::String& custom_idea,
                                                     const container::String& previous_error,
                                                     const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Revise the high-level plan options because the user rejected the current candidates.");
    append_line(prompt, "Return JSON only; no markdown or prose. Generate top-level options only, not detailed steps.");
    append_line(prompt, R"(Schema: {"title":"...","objective":"...","options":[{"id":"option_1","title":"...","summary":"... include tradeoffs/when-to-use concisely ...","recommended":true,"items":[]}]})");
    append_line(prompt, "Preserve the objective, but adapt the candidate approaches to the user's custom idea. Provide 2-4 distinct options when useful.");
    append_line(prompt, "Current plan/options JSON:");
    append_json_line(prompt, draft);
    append_line(prompt, "User custom idea:");
    append_line(prompt, std::string_view(custom_idea.data(), custom_idea.size()));
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_decision_revision_prompt(const PlanDraft& draft,
                                                      const container::String& item_id,
                                                      const container::String& decision_id,
                                                      const container::String& custom_idea,
                                                      const container::String& previous_error,
                                                      const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Revise the detailed plan because the user rejected all choices for one decision.");
    append_line(prompt, "Return JSON only; no markdown or prose. Keep the output in the detailed plan schema.");
    append_line(prompt, R"(Schema: {"title":"...","objective":"...","selected_option_id":"option_1","items":[{"id":"step_1","title":"...","description":"...","required":true,"risks":["..."],"validation":["..."],"decisions":[{"id":"decision_1","title":"...","description":"...","required":true,"choices":[{"id":"choice_1","title":"...","description":"...","recommended":true}],"selected_choice_id":"","custom_note":""}]}],"global_risks":["..."],"validation":["..."]})");
    append_line(prompt, "Preserve unrelated steps and resolved decisions. For the target decision, either generate better choices or encode the user's idea as custom_note. Do not finalize the plan.");
    append_line(prompt, "Latency budget: keep unchanged content compact; at most 3 required decisions total, each with 2 choices; descriptions under 35 words.");
    append_line(prompt, "Current detailed plan JSON:");
    append_json_line(prompt, draft);
    append_line(prompt, "Target item id:");
    append_line(prompt, std::string_view(item_id.data(), item_id.size()));
    append_line(prompt, "Target decision id:");
    append_line(prompt, std::string_view(decision_id.data(), decision_id.size()));
    append_line(prompt, "User custom idea:");
    append_line(prompt, std::string_view(custom_idea.data(), custom_idea.size()));
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_final_revision_prompt(const PlanDraft& draft,
                                                   const container::String& custom_idea,
                                                   const container::String& previous_error,
                                                   const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Revise the final synthesized plan using the user's feedback.");
    append_line(prompt, "Return JSON only; no markdown or prose. Do not introduce unresolved decisions.");
    append_line(prompt, R"(Schema: {"final_summary":"...","final_items":[{"id":"step_1","title":"...","description":"... include selected decisions ...","required":true,"risks":["..."],"validation":["..."]}],"global_risks":["..."],"validation":["..."],"consistency_notes":["..."]})");
    append_line(prompt, "Base the revision on the current final plan and the user feedback. Keep user-selected decisions intact unless the feedback explicitly changes them.");
    append_line(prompt, "Current final plan JSON:");
    append_json_line(prompt, draft);
    append_line(prompt, "User feedback:");
    append_line(prompt, std::string_view(custom_idea.data(), custom_idea.size()));
    if (!previous_error.empty()) {
        append_line(prompt, "Previous output was invalid. Fix this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

container::String build_plan_generation_prompt(const container::String& objective,
                                               const container::String& user_note,
                                               const container::String& previous_error,
                                               const container::String& previous_output) {
    return build_plan_options_prompt(objective, user_note, previous_error, previous_output);
}

} // namespace ben_gear::orchestration
