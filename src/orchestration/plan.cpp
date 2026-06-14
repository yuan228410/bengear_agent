#include "ben_gear/orchestration/plan.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace ben_gear::orchestration {

namespace {

container::String make_id(std::string_view prefix, uint64_t seed) {
    std::string value(prefix);
    value.push_back(':');
    value += std::to_string(seed);
    return container::String(value.data(), value.size());
}

bool is_terminal(PlanStatus status) {
    return status == PlanStatus::cancelled || status == PlanStatus::failed;
}

} // namespace

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

bool PlanManager::is_active() const noexcept {
    return draft_.status != PlanStatus::idle && !is_terminal(draft_.status);
}

bool PlanManager::is_reviewing() const noexcept {
    return draft_.status == PlanStatus::drafting || draft_.status == PlanStatus::reviewing;
}

bool PlanManager::is_executing() const noexcept {
    return draft_.status == PlanStatus::confirmed || draft_.status == PlanStatus::executing;
}

bool PlanManager::read_only_tools() const noexcept {
    return is_reviewing();
}

const PlanDraft& PlanManager::start(const PlanCommand& command) {
    draft_ = {};
    draft_.plan_id = command.plan_id.empty() ? make_id("plan", now_ms()) : command.plan_id;
    draft_.session_id = command.session_id;
    draft_.workspace = command.workspace;
    draft_.objective = command.prompt;
    draft_.status = PlanStatus::drafting;
    draft_.revision = 1;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::mark_drafting() {
    draft_.status = PlanStatus::drafting;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::apply_model_draft(container::String title,
                                                container::String objective,
                                                container::Vector<PlanItem> items) {
    draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    normalize_items(items);
    draft_.items = std::move(items);
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::apply_model_options(container::String title,
                                                  container::String objective,
                                                  container::Vector<PlanOption> options,
                                                  container::String selected_option_id) {
    draft_.title = std::move(title);
    if (!objective.empty()) draft_.objective = std::move(objective);
    int option_order = 1;
    for (auto& option : options) {
        if (option.id.empty()) option.id = make_id("option", static_cast<uint64_t>(option_order));
        normalize_items(option.items);
        ++option_order;
    }
    draft_.options = std::move(options);
    if (selected_option_id.empty() && !draft_.options.empty()) {
        for (const auto& option : draft_.options) {
            if (option.recommended) {
                selected_option_id = option.id;
                break;
            }
        }
        if (selected_option_id.empty()) selected_option_id = draft_.options[0].id;
    }
    draft_.selected_option_id = selected_option_id;
    draft_.items = {};
    for (const auto& option : draft_.options) {
        if (option.id == draft_.selected_option_id) {
            draft_.items = option.items;
            break;
        }
    }
    if (draft_.items.empty() && !draft_.options.empty()) {
        draft_.selected_option_id = {};
        for (const auto& option : draft_.options) {
            if (option.recommended) {
                draft_.selected_option_id = option.id;
                draft_.items = option.items;
                break;
            }
        }
        if (draft_.items.empty()) {
            draft_.selected_option_id = draft_.options[0].id;
            draft_.items = draft_.options[0].items;
        }
    }
    draft_.status = PlanStatus::reviewing;
    draft_.error = {};
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::select_option(container::String option_id) {
    if (!is_reviewing()) {
        throw std::logic_error("plan option can only be selected while reviewing");
    }
    for (const auto& option : draft_.options) {
        if (option.id == option_id) {
            draft_.selected_option_id = std::move(option_id);
            draft_.items = option.items;
            bump_revision();
            return draft_;
        }
    }
    throw std::logic_error("plan option not found");
}

const PlanDraft& PlanManager::apply_user_items(container::Vector<PlanItem> items) {
    if (!is_reviewing()) {
        throw std::logic_error("plan items can only be edited while reviewing");
    }
    normalize_items(items);
    draft_.items = std::move(items);
    draft_.selected_option_id = {};
    draft_.status = PlanStatus::reviewing;
    bump_revision();
    return draft_;
}

const PlanDraft& PlanManager::mark_failed(container::String error) {
    draft_.status = PlanStatus::failed;
    draft_.error = std::move(error);
    touch();
    return draft_;
}

const PlanDraft& PlanManager::confirm(int revision) {
    if (draft_.status != PlanStatus::reviewing) {
        throw std::logic_error("plan is not ready for confirmation");
    }
    if (revision != draft_.revision) {
        throw std::logic_error("stale plan revision");
    }
    if (draft_.items.empty()) {
        throw std::logic_error("plan must contain at least one item");
    }
    draft_.status = PlanStatus::confirmed;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::mark_executing() {
    if (draft_.status != PlanStatus::confirmed && draft_.status != PlanStatus::executing) {
        throw std::logic_error("plan must be confirmed before execution");
    }
    draft_.status = PlanStatus::executing;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::cancel() {
    draft_.status = PlanStatus::cancelled;
    touch();
    return draft_;
}

const PlanDraft& PlanManager::restore(PlanDraft draft) {
    normalize_items(draft.items);
    for (auto& option : draft.options) {
        normalize_items(option.items);
    }
    draft_ = std::move(draft);
    if (draft_.updated_ms == 0) touch();
    return draft_;
}

void PlanManager::reset() {
    draft_ = {};
}

void PlanManager::bump_revision() {
    ++draft_.revision;
    touch();
}

void PlanManager::touch() {
    draft_.updated_ms = now_ms();
}

void PlanManager::normalize_items(container::Vector<PlanItem>& items) const {
    int order = 1;
    for (auto& item : items) {
        if (item.id.empty()) {
            item.id = make_id("plan_item", static_cast<uint64_t>(order));
        }
        item.order = order++;
        if (item.title.empty()) {
            item.title = item.description.empty() ? container::String("Untitled task") : item.description;
        }
        int choice_order = 1;
        for (auto& choice : item.choices) {
            if (choice.id.empty()) {
                choice.id = make_id("choice", static_cast<uint64_t>(choice_order));
            }
            if (choice.title.empty()) {
                choice.title = choice.description.empty() ? container::String("Default option") : choice.description;
            }
            if (item.selected_choice_id.empty() && choice.recommended) {
                item.selected_choice_id = choice.id;
            }
            ++choice_order;
        }
        if (item.selected_choice_id.empty() && !item.choices.empty()) {
            item.selected_choice_id = item.choices[0].id;
        }
    }
}

} // namespace ben_gear::orchestration
