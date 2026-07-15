#pragma once

#include "orchestration/plan.hpp"

#include <string_view>

namespace ben_gear::orchestration {

struct PlanParseResult {
    bool ok = false;
    PlanDraft draft;
    std::string error;
};

PlanParseResult parse_plan_draft_text(std::string_view text,
                                      const std::string& session_id,
                                      const std::string& workspace,
                                      const std::string& fallback_objective);

PlanParseResult parse_plan_options_text(std::string_view text,
                                        const std::string& session_id,
                                        const std::string& workspace,
                                        const std::string& fallback_objective);

PlanParseResult parse_plan_detail_text(std::string_view text,
                                       const std::string& session_id,
                                       const std::string& workspace,
                                       const std::string& fallback_objective,
                                       const std::string& selected_option_id);

PlanParseResult parse_plan_final_text(std::string_view text,
                                      const PlanDraft& source_draft);

std::string build_plan_generation_prompt(const std::string& objective,
                                               const std::string& user_note = {},
                                               const std::string& previous_error = {},
                                               const std::string& previous_output = {});

std::string build_plan_options_prompt(const std::string& objective,
                                            const std::string& user_note = {},
                                            const std::string& previous_error = {},
                                            const std::string& previous_output = {});

std::string build_plan_detail_prompt(const PlanDraft& draft,
                                           const std::string& selected_option_id,
                                           const std::string& previous_error = {},
                                           const std::string& previous_output = {});

std::string build_plan_finalization_prompt(const PlanDraft& draft,
                                                 const std::string& previous_error = {},
                                                 const std::string& previous_output = {});

std::string build_plan_options_revision_prompt(const PlanDraft& draft,
                                                     const std::string& custom_idea,
                                                     const std::string& previous_error = {},
                                                     const std::string& previous_output = {});

std::string build_plan_decision_revision_prompt(const PlanDraft& draft,
                                                      const std::string& item_id,
                                                      const std::string& decision_id,
                                                      const std::string& custom_idea,
                                                      const std::string& previous_error = {},
                                                      const std::string& previous_output = {});

std::string build_plan_final_revision_prompt(const PlanDraft& draft,
                                                   const std::string& custom_idea,
                                                   const std::string& previous_error = {},
                                                   const std::string& previous_output = {});

} // namespace ben_gear::orchestration
