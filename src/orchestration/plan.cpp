#include "orchestration/plan.hpp"
#include <mutex>
#include "memory/prompt_mode.hpp"
#include "domain/errors.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace ben_gear::orchestration {

namespace {

std::string make_id(std::string_view prefix, uint64_t seed) {
    std::string value(prefix);
    value.push_back(':');
    value += std::to_string(seed);
    return value;
}

bool is_terminal(PlanStatus status) {
    return status == PlanStatus::cancelled || status == PlanStatus::failed;
}

bool choice_resolved(const std::string& selected_choice_id, const std::string& custom_note) {
    return !selected_choice_id.empty() || !custom_note.empty();
}

} // namespace

void PlanManager::require(PlanStatus expected) const {
    if (draft_.status != expected) throw domain::AppError::invalid_argument("UNEXPECTED_STATUS",
        std::string("plan: expected ") + to_string(expected) + ", current " + to_string(draft_.status));
}

void PlanManager::require_any(std::initializer_list<PlanStatus> allowed) const {
    for (auto s : allowed) if (draft_.status == s) return;
    throw domain::AppError::invalid_argument("UNEXPECTED_STATUS", std::string("plan: unexpected status ") + to_string(draft_.status));
}

void PlanManager::set_status(PlanStatus s) { draft_.status = s; touch(); }

uint64_t now_ms() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

const char* to_string(PlanStatus status) {
    switch (status) {
    case PlanStatus::idle: return "idle";
    case PlanStatus::drafting: return "drafting";
    case PlanStatus::reviewing: return "reviewing";
    case PlanStatus::confirmed: return "confirmed";
    case PlanStatus::executing: return "executing";
    case PlanStatus::cancelled: return "cancelled";
    case PlanStatus::failed: return "failed";
    }
    return "failed";
}

PlanStatus plan_status_from_string(std::string_view value) {
    if (value == "idle") return PlanStatus::idle;
    if (value == "drafting") return PlanStatus::drafting;
    if (value == "reviewing") return PlanStatus::reviewing;
    if (value == "confirmed") return PlanStatus::confirmed;
    if (value == "executing") return PlanStatus::executing;
    if (value == "cancelled") return PlanStatus::cancelled;
    return PlanStatus::failed;
}

const char* to_string(PlanStage stage) {
    switch (stage) {
    case PlanStage::idle: return "idle";
    case PlanStage::option_review: return "option_review";
    case PlanStage::detailing: return "detailing";
    case PlanStage::decision_review: return "decision_review";
    case PlanStage::finalizing: return "finalizing";
    case PlanStage::final_review: return "final_review";
    }
    return "idle";
}

PlanStage plan_stage_from_string(std::string_view value) {
    if (value == "option_review") return PlanStage::option_review;
    if (value == "detailing") return PlanStage::detailing;
    if (value == "decision_review") return PlanStage::decision_review;
    if (value == "finalizing") return PlanStage::finalizing;
    if (value == "final_review") return PlanStage::final_review;
    return PlanStage::idle;
}

bool PlanManager::is_active() const noexcept {
    return draft_.status != PlanStatus::idle && !is_terminal(draft_.status);
}

bool PlanManager::is_reviewing() const noexcept {
    return draft_.status == PlanStatus::drafting || draft_.status == PlanStatus::reviewing;
}

bool PlanManager::is_executing() const noexcept {
    return draft_.status == PlanStatus::confirmed || draft_.status == PlanStatus::executing;
}

memory::PromptMode PlanManager::current_prompt_mode() const noexcept {
    if (draft_.status == PlanStatus::reviewing || draft_.status == PlanStatus::drafting)
        return memory::PromptMode::plan_reviewing;
    if (draft_.status == PlanStatus::executing || draft_.status == PlanStatus::confirmed)
        return memory::PromptMode::plan_executing;
    return memory::PromptMode::normal;
}

bool PlanManager::read_only_tools() const noexcept {
    return is_reviewing();
}

bool PlanManager::all_decisions_resolved() const noexcept {
    if (draft_.items.empty()) return false;
    for (const auto& item : draft_.items) {
        for (const auto& decision : item.decisions) {
            if (decision.required && !choice_resolved(decision.selected_choice_id, decision.custom_note)) {
                return false;
            }
        }
    }
    return true;
}

const PlanDraft& PlanManager::start(const PlanCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_ = {};
    draft_.plan_id = command.plan_id.empty() ? make_id("plan", now_ms()) : command.plan_id;
    draft_.session_id = command.session_id;
    draft_.workspace = command.workspace;
    draft_.objective = command.prompt;
    draft_.status = PlanStatus::drafting;
    draft_.stage = PlanStage::idle;
    draft_.revision = 1;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::mark_drafting() {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_.status = PlanStatus::drafting;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::apply_model_draft(std::string title,
                                                std::string objective,
                                                std::vector<PlanItem> items) {
    std::lock_guard<std::mutex> lock(mutex_);
    require(PlanStatus::drafting);
    draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    normalize_items(items, true);
    draft_.items = std::move(items);
    draft_.stage = PlanStage::decision_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    clear_final_fields();
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::apply_model_options(std::string title,
                                                  std::string objective,
                                                  std::vector<PlanOption> options,
                                                  std::string selected_option_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    int option_order = 1;
    for (auto& option : options) {
        if (option.id.empty()) option.id = make_id("option", static_cast<uint64_t>(option_order));
        // 顶层方案阶段只保留候选方案摘要；不把步骤细节提前绑定到 UI。
        normalize_items(option.items, false);
        ++option_order;
    }
    draft_.options = std::move(options);
    draft_.selected_option_id = std::move(selected_option_id);
    draft_.detailed_option_id = {};
    draft_.items = {};
    draft_.global_risks = {};
    draft_.validation = {};
    clear_final_fields();
    draft_.stage = PlanStage::option_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

uint64_t PlanManager::begin_detailing(std::string option_id, int revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.stage != PlanStage::option_review || draft_.status != PlanStatus::reviewing) {
        throw domain::AppError::invalid_argument("STATE", "plan option can only be selected during option review");
    }
    if (revision != draft_.revision) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision");
    }
    bool found = false;
    for (const auto& option : draft_.options) {
        if (option.id == option_id) {
            found = true;
            break;
        }
    }
    if (!found) throw domain::AppError::not_found("OPTION_NOT_FOUND", "plan option not found");
    draft_.selected_option_id = std::move(option_id);
    draft_.detailed_option_id = {};
    draft_.items = {};
    draft_.stage = PlanStage::detailing;
    draft_.status = PlanStatus::drafting;
    clear_final_fields();
    const auto request_id = next_request_id();
    bump_revision();
    return request_id;
}

const PlanDraft& PlanManager::apply_model_detail(std::string option_id,
                                                 uint64_t request_id,
                                                 std::string title,
                                                 std::string objective,
                                                 std::vector<PlanItem> items,
                                                 std::vector<std::string> global_risks,
                                                 std::vector<std::string> validation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_id != draft_.planning_request_id || option_id != draft_.selected_option_id) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan detail result");
    }
    if (items.empty()) throw domain::AppError::invalid_argument("EMPTY_ITEMS", "detailed plan must contain at least one item");
    if (!title.empty()) draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    normalize_items(items, false);
    draft_.items = std::move(items);
    draft_.detailed_option_id = std::move(option_id);
    draft_.global_risks = std::move(global_risks);
    draft_.validation = std::move(validation);
    draft_.stage = PlanStage::decision_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    clear_final_fields();
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::select_option(std::string option_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.stage == PlanStage::option_review) {
        begin_detailing(std::move(option_id), draft_.revision);
        return draft_;
    }
    if (!is_reviewing()) {
        throw domain::AppError::invalid_argument("STATE", "plan option can only be selected while reviewing");
    }
    for (const auto& option : draft_.options) {
        if (option.id == option_id) {
            draft_.selected_option_id = std::move(option_id);
            draft_.items = option.items;
            normalize_items(draft_.items, false);
            draft_.stage = PlanStage::decision_review;
            draft_.status = PlanStatus::reviewing;
            clear_final_fields();
            bump_revision();
            return draft_;
        }
    }
    throw domain::AppError::not_found("OPTION_NOT_FOUND", "plan option not found");
}

const PlanDraft& PlanManager::apply_user_items(std::vector<PlanItem> items) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_reviewing()) {
        throw domain::AppError::invalid_argument("STATE", "plan items can only be edited while reviewing");
    }
    normalize_items(items, false);
    draft_.items = std::move(items);
    draft_.selected_option_id = {};
    draft_.stage = PlanStage::decision_review;
    draft_.status = PlanStatus::reviewing;
    clear_final_fields();
    bump_revision();
    return draft_;
}

bool PlanManager::apply_decision(const PlanDecisionPatch& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status != PlanStatus::reviewing ||
        (draft_.stage != PlanStage::decision_review && draft_.stage != PlanStage::final_review)) {
        throw domain::AppError::invalid_argument("STATE", "plan decision can only be applied during decision review");
    }
    if (patch.revision != draft_.revision) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision");
    }
    for (auto& item : draft_.items) {
        if (item.id != patch.item_id) continue;
        for (auto& decision : item.decisions) {
            if (decision.id != patch.decision_id) continue;
            decision.selected_choice_id = patch.choice_id;
            decision.custom_note = patch.custom_note;
            if (draft_.stage == PlanStage::final_review) {
                draft_.stage = PlanStage::decision_review;
                clear_final_fields();
            }
            bump_revision();
            return all_decisions_resolved();
        }
        throw domain::AppError::not_found("DECISION_NOT_FOUND", "plan decision not found");
    }
    throw domain::AppError::not_found("ITEM_NOT_FOUND", "plan item not found");
}

uint64_t PlanManager::begin_chat_revision(int revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status != PlanStatus::reviewing ||
        (draft_.stage != PlanStage::option_review && draft_.stage != PlanStage::decision_review && draft_.stage != PlanStage::final_review)) {
        throw domain::AppError::invalid_argument("STATE", "plan revision can only be requested while reviewing");
    }
    if (revision != 0 && revision != draft_.revision) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision");
    }
    draft_.status = PlanStatus::drafting;
    const auto request_id = next_request_id();
    touch();
    return request_id;
}

const PlanDraft& PlanManager::apply_revised_options(uint64_t request_id,
                                                     std::string title,
                                                     std::string objective,
                                                     std::vector<PlanOption> options,
                                                     std::string selected_option_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_id != draft_.planning_request_id) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision result");
    }
    draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    int option_order = 1;
    for (auto& option : options) {
        if (option.id.empty()) option.id = make_id("option", static_cast<uint64_t>(option_order));
        normalize_items(option.items, false);
        ++option_order;
    }
    draft_.options = std::move(options);
    draft_.selected_option_id = std::move(selected_option_id);
    draft_.detailed_option_id = {};
    draft_.items = {};
    draft_.global_risks = {};
    draft_.validation = {};
    clear_final_fields();
    draft_.stage = PlanStage::option_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::apply_revised_detail(uint64_t request_id,
                                                    std::string title,
                                                    std::string objective,
                                                    std::vector<PlanItem> items,
                                                    std::vector<std::string> global_risks,
                                                    std::vector<std::string> validation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_id != draft_.planning_request_id) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision result");
    }
    if (items.empty()) throw domain::AppError::invalid_argument("EMPTY_ITEMS", "detailed plan must contain at least one item");
    if (!title.empty()) draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    normalize_items(items, false);
    draft_.items = std::move(items);
    draft_.global_risks = std::move(global_risks);
    draft_.validation = std::move(validation);
    draft_.stage = PlanStage::decision_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    clear_final_fields();
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::apply_revised_final(uint64_t request_id, PlanFinalDraft final_draft) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_id != draft_.planning_request_id) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan final revision result");
    }
    if (final_draft.items.empty()) throw domain::AppError::invalid_argument("EMPTY_ITEMS", "final plan must contain at least one item");
    normalize_items(final_draft.items, false);
    draft_.final_summary = std::move(final_draft.summary);
    draft_.final_items = std::move(final_draft.items);
    draft_.global_risks = std::move(final_draft.global_risks);
    draft_.validation = std::move(final_draft.validation);
    draft_.consistency_notes = std::move(final_draft.consistency_notes);
    draft_.finalized_input_revision = draft_.revision;
    draft_.stage = PlanStage::final_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

uint64_t PlanManager::begin_finalizing(int revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status == PlanStatus::reviewing && draft_.stage == PlanStage::final_review &&
        draft_.finalized_input_revision == revision) {
        return draft_.planning_request_id;
    }
    if (draft_.status != PlanStatus::reviewing || draft_.stage != PlanStage::decision_review) {
        throw domain::AppError::invalid_argument("STATE", "plan can only be finalized during decision review");
    }
    if (revision != draft_.revision) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision");
    }
    if (!all_decisions_resolved()) {
        throw domain::AppError::invalid_argument("STATE", "all required plan decisions must be resolved before finalization");
    }
    draft_.stage = PlanStage::finalizing;
    draft_.status = PlanStatus::drafting;
    const auto request_id = next_request_id();
    touch();
    return request_id;
}

const PlanDraft& PlanManager::apply_model_final(uint64_t request_id, PlanFinalDraft final_draft) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (request_id != draft_.planning_request_id || draft_.stage != PlanStage::finalizing) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan finalization result");
    }
    if (final_draft.items.empty()) throw domain::AppError::invalid_argument("EMPTY_ITEMS", "final plan must contain at least one item");
    normalize_items(final_draft.items, false);
    draft_.final_summary = std::move(final_draft.summary);
    draft_.final_items = std::move(final_draft.items);
    draft_.global_risks = std::move(final_draft.global_risks);
    draft_.validation = std::move(final_draft.validation);
    draft_.consistency_notes = std::move(final_draft.consistency_notes);
    draft_.finalized_input_revision = draft_.revision;
    draft_.stage = PlanStage::final_review;
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::mark_failed(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_.status = PlanStatus::failed;
    draft_.error = std::move(error);
    touch();
    return draft_;
}

const PlanDraft& PlanManager::mark_review_error(std::string error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status == PlanStatus::drafting) draft_.status = PlanStatus::reviewing;
    draft_.error = std::move(error);
    touch();
    return draft_;
}

const PlanDraft& PlanManager::confirm(int revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status != PlanStatus::reviewing || draft_.stage != PlanStage::final_review) {
        throw domain::AppError::invalid_argument("STATE", "final plan is not ready for confirmation");
    }
    if (revision != draft_.revision) {
        throw domain::AppError::conflict("STALE_REVISION", "stale plan revision");
    }
    if (draft_.final_items.empty()) {
        throw domain::AppError::invalid_argument("EMPTY_ITEMS", "final plan must contain at least one item");
    }
    draft_.status = PlanStatus::confirmed;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::confirm_simple() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status != PlanStatus::reviewing) {
        throw domain::AppError::invalid_argument("STATE", "plan must be in reviewing state to confirm");
    }
    if (draft_.items.empty()) {
        throw domain::AppError::invalid_argument("EMPTY_ITEMS", "plan must contain at least one item");
    }
    draft_.status = PlanStatus::confirmed;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::mark_executing() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (draft_.status != PlanStatus::confirmed && draft_.status != PlanStatus::executing) {
        throw domain::AppError::invalid_argument("STATE", "plan must be confirmed before execution");
    }
    draft_.status = PlanStatus::executing;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_.status = PlanStatus::cancelled;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::restore(PlanDraft draft) {
    std::lock_guard<std::mutex> lock(mutex_);
    normalize_items(draft.items, false);
    normalize_items(draft.final_items, false);
    for (auto& option : draft.options) {
        normalize_items(option.items, false);
    }
    if (draft.stage == PlanStage::idle && draft.status == PlanStatus::reviewing) {
        draft.stage = draft.options.empty() ? PlanStage::decision_review : PlanStage::option_review;
    }
    draft_ = std::move(draft);
    if (draft_.updated_ms == 0) touch();
    return draft_;
}

void PlanManager::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    draft_ = {};
}

void PlanManager::bump_revision() {
    ++draft_.revision;
    touch();
}

void PlanManager::touch() {
    draft_.updated_ms = now_ms();
}

uint64_t PlanManager::next_request_id() {
    ++draft_.planning_request_id;
    if (draft_.planning_request_id == 0) draft_.planning_request_id = 1;
    return draft_.planning_request_id;
}

void PlanManager::clear_final_fields() {
    draft_.final_summary = {};
    draft_.final_items = {};
    draft_.consistency_notes = {};
    draft_.finalized_input_revision = 0;
}

void PlanManager::normalize_decisions(std::vector<PlanDecision>& decisions) const {
    int decision_order = 1;
    for (auto& decision : decisions) {
        if (decision.id.empty()) decision.id = make_id("decision", static_cast<uint64_t>(decision_order));
        if (decision.title.empty()) {
            decision.title = decision.description.empty() ? std::string("Decision") : decision.description;
        }
        int choice_order = 1;
        for (auto& choice : decision.choices) {
            if (choice.id.empty()) choice.id = make_id("choice", static_cast<uint64_t>(choice_order));
            if (choice.title.empty()) {
                choice.title = choice.description.empty() ? std::string("Option") : choice.description;
            }
            ++choice_order;
        }
        ++decision_order;
    }
}

void PlanManager::normalize_items(std::vector<PlanItem>& items, bool select_recommended_choices) const {
    int order = 1;
    for (auto& item : items) {
        if (item.id.empty()) {
            item.id = make_id("plan_item", static_cast<uint64_t>(order));
        }
        item.order = order++;
        if (item.title.empty()) {
            item.title = item.description.empty() ? std::string("Untitled task") : item.description;
        }
        int choice_order = 1;
        for (auto& choice : item.choices) {
            if (choice.id.empty()) {
                choice.id = make_id("choice", static_cast<uint64_t>(choice_order));
            }
            if (choice.title.empty()) {
                choice.title = choice.description.empty() ? std::string("Default option") : choice.description;
            }
            if (select_recommended_choices && item.selected_choice_id.empty() && choice.recommended) {
                item.selected_choice_id = choice.id;
            }
            ++choice_order;
        }
        if (select_recommended_choices && item.selected_choice_id.empty() && !item.choices.empty()) {
            item.selected_choice_id = item.choices[0].id;
        }
        normalize_decisions(item.decisions);
    }
}

} // namespace ben_gear::orchestration
