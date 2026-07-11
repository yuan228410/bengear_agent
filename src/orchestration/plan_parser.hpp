#pragma once

#include "orchestration/plan.hpp"

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

PlanParseResult parse_plan_options_text(std::string_view text,
                                        const container::String& session_id,
                                        const container::String& workspace,
                                        const container::String& fallback_objective);

PlanParseResult parse_plan_detail_text(std::string_view text,
                                       const container::String& session_id,
                                       const container::String& workspace,
                                       const container::String& fallback_objective,
                                       const container::String& selected_option_id);

PlanParseResult parse_plan_final_text(std::string_view text,
                                      const PlanDraft& source_draft);

container::String build_plan_generation_prompt(const container::String& objective,
                                               const container::String& user_note = {},
                                               const container::String& previous_error = {},
                                               const container::String& previous_output = {});

container::String build_plan_options_prompt(const container::String& objective,
                                            const container::String& user_note = {},
                                            const container::String& previous_error = {},
                                            const container::String& previous_output = {});

container::String build_plan_detail_prompt(const PlanDraft& draft,
                                           const container::String& selected_option_id,
                                           const container::String& previous_error = {},
                                           const container::String& previous_output = {});

container::String build_plan_finalization_prompt(const PlanDraft& draft,
                                                 const container::String& previous_error = {},
                                                 const container::String& previous_output = {});

container::String build_plan_options_revision_prompt(const PlanDraft& draft,
                                                     const container::String& custom_idea,
                                                     const container::String& previous_error = {},
                                                     const container::String& previous_output = {});

container::String build_plan_decision_revision_prompt(const PlanDraft& draft,
                                                      const container::String& item_id,
                                                      const container::String& decision_id,
                                                      const container::String& custom_idea,
                                                      const container::String& previous_error = {},
                                                      const container::String& previous_output = {});

container::String build_plan_final_revision_prompt(const PlanDraft& draft,
                                                   const container::String& custom_idea,
                                                   const container::String& previous_error = {},
                                                   const container::String& previous_output = {});

} // namespace ben_gear::orchestration
