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
        if (escaping) {
            escaping = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) continue;
        if (ch == open) ++depth;
        if (ch == close) {
            --depth;
            if (depth == 0) return to_std(text.substr(start, i - start + 1));
        }
    }
    return to_std(text.substr(start));
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
    }
    return true;
}

bool valid_draft(const PlanDraft& draft, container::String& error) {
    if (!draft.options.empty()) {
        for (const auto& option : draft.options) {
            if (!valid_items(option.items, error)) return false;
        }
        return true;
    }
    return valid_items(draft.items, error);
}

void append_line(std::string& text, std::string_view line) {
    text.append(line.data(), line.size());
    text.push_back('\n');
}

} // namespace

PlanParseResult parse_plan_draft_text(std::string_view text,
                                      const container::String& session_id,
                                      const container::String& workspace,
                                      const container::String& fallback_objective) {
    PlanParseResult result;
    auto json_text = extract_json_text(text);
    std::string error;
    auto json = parse_json(json_text, error);
    if (!error.empty() || !json.is_object()) {
        result.error = error.empty() ? container::String("LLM output is not a JSON object") : container::String(error.c_str());
        return result;
    }

    auto parsed = plan_draft_from_json(json);
    parsed.session_id = session_id;
    parsed.workspace = workspace;
    if (parsed.objective.empty()) parsed.objective = fallback_objective;
    if (parsed.title.empty()) parsed.title = "Execution plan";

    container::String validation_error;
    if (!valid_draft(parsed, validation_error)) {
        result.error = validation_error;
        return result;
    }

    PlanManager manager;
    PlanCommand command;
    command.session_id = session_id;
    command.workspace = workspace;
    command.prompt = parsed.objective;
    manager.start(command);
    if (!parsed.options.empty()) {
        manager.apply_model_options(std::move(parsed.title),
                                    std::move(parsed.objective),
                                    std::move(parsed.options),
                                    std::move(parsed.selected_option_id));
    } else {
        manager.apply_model_draft(std::move(parsed.title),
                                  std::move(parsed.objective),
                                  std::move(parsed.items));
    }
    result.draft = manager.draft();
    result.ok = true;
    return result;
}

container::String build_plan_generation_prompt(const container::String& objective,
                                               const container::String& user_note,
                                               const container::String& previous_error,
                                               const container::String& previous_output) {
    std::string prompt;
    append_line(prompt, "Create a concise plan review JSON for the user's objective.");
    append_line(prompt, "Return JSON only; no markdown or prose.");
    append_line(prompt, R"(Schema: {"title":"...","objective":"...","options":[{"id":"option_1","title":"...","summary":"...","recommended":true,"items":[{"id":"step_1","title":"...","description":"...","required":true,"choices":[{"id":"choice_1","title":"...","description":"...","recommended":true}],"selected_choice_id":"choice_1","custom_note":""}]}],"selected_option_id":"option_1","items":[]})");
    append_line(prompt, "Model decides granularity: simple goals can be one option and one item. Do not add choices/custom_note for routine steps; use them only when the user needs to choose between real alternatives. The final plan is confirmed once overall, not step-by-step.");
    append_line(prompt, "Objective:");
    append_line(prompt, std::string_view(objective.data(), objective.size()));
    if (!user_note.empty()) {
        append_line(prompt, "User revision note:");
        append_line(prompt, std::string_view(user_note.data(), user_note.size()));
    }
    if (!previous_error.empty()) {
        append_line(prompt, "Your previous output was invalid. Fix it according to this validation error:");
        append_line(prompt, std::string_view(previous_error.data(), previous_error.size()));
        append_line(prompt, "Previous output excerpt:");
        append_line(prompt, compact_output(previous_output));
    }
    return container::String(prompt.c_str(), prompt.size());
}

} // namespace ben_gear::orchestration
