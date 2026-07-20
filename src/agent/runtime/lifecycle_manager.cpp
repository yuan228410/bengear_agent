#include "agent/runtime/lifecycle_manager.hpp"

#include "log/logger.hpp"

namespace ben_gear::agent::runtime {

void LifecycleManager::add_observer(std::shared_ptr<ILifecycleObserver> observer) {
    observers_.push_back(std::move(observer));
}

void LifecycleManager::remove_observer(ILifecycleObserver* observer) {
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [observer](const std::shared_ptr<ILifecycleObserver>& o) {
                return o.get() == observer;
            }),
        observers_.end());
}

void LifecycleManager::begin_initialization(const std::string& component_name) {
    if (phase_ != LifecyclePhase::Created) {
        log::error_fmt("LifecycleManager: cannot begin initialization from phase {}",
                       static_cast<int>(phase_));
        return;
    }
    phase_ = LifecyclePhase::Initializing;
    notify_observers(LifecycleEvent::BeforeInit, component_name);
}

void LifecycleManager::end_initialization(const std::string& component_name) {
    if (phase_ != LifecyclePhase::Initializing) {
        log::error_fmt("LifecycleManager: cannot end initialization from phase {}",
                       static_cast<int>(phase_));
        return;
    }
    phase_ = LifecyclePhase::Ready;
    notify_observers(LifecycleEvent::AfterInit, component_name);
    log::info_fmt("LifecycleManager: initialization complete, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::begin_shutdown(const std::string& component_name) {
    if (phase_ != LifecyclePhase::Ready) {
        log::error_fmt("LifecycleManager: cannot begin shutdown from phase {}",
                       static_cast<int>(phase_));
        return;
    }
    phase_ = LifecyclePhase::ShuttingDown;
    notify_observers(LifecycleEvent::BeforeShutdown, component_name);
    log::info_fmt("LifecycleManager: shutdown started, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::end_shutdown(const std::string& component_name) {
    if (phase_ != LifecyclePhase::ShuttingDown) {
        log::error_fmt("LifecycleManager: cannot end shutdown from phase {}",
                       static_cast<int>(phase_));
        return;
    }
    phase_ = LifecyclePhase::Destroyed;
    notify_observers(LifecycleEvent::AfterShutdown, component_name);
    log::info_fmt("LifecycleManager: shutdown complete, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::force_phase(LifecyclePhase phase) {
    log::info_fmt("LifecycleManager: force phase {} -> {}",
                  static_cast<int>(phase_), static_cast<int>(phase));
    phase_ = phase;
}

void LifecycleManager::notify_observers(LifecycleEvent event,
                                        const std::string& component_name) {
    for (auto& observer : observers_) {
        try {
            observer->on_lifecycle_event(event, component_name);
        } catch (const std::exception& e) {
            log::error_fmt("LifecycleManager: observer threw exception: {}", e.what());
        }
    }
}

} // namespace ben_gear::agent::runtime
