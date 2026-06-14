#pragma once

#include "ben_gear/orchestration/plan.hpp"

#include <string_view>

namespace ben_gear::orchestration {

struct PlanParseResult {
    bool ok = false;
    PlanDraft draft;
    container::String error;
};

PlanParseResult parse_plan_draft_text(std::string_view text,
                                      const container::String& session_id,
                                      const container::String& workspace,
                                      const container::String& fallback_objective);

container::String build_plan_generation_prompt(const container::String& objective,
                                               const container::String& user_note = {},
                                               const container::String& previous_error = {},
                                               const container::String& previous_output = {});

} // namespace ben_gear::orchestration
