#include "agent/runtime/lifecycle_manager.hpp"

#include "log/logger.hpp"

namespace ben_gear::agent::runtime {

void LifecycleManager::add_observer(std::shared_ptr<ILifecycleObserver> observer) {
    std::lock_guard lock(mutex_);
    observers_.push_back(std::move(observer));
}

void LifecycleManager::remove_observer(ILifecycleObserver* observer) {
    std::lock_guard lock(mutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
            [observer](const std::shared_ptr<ILifecycleObserver>& o) {
                return o.get() == observer;
            }),
        observers_.end());
}

void LifecycleManager::begin_initialization(const std::string& component_name) {
    std::vector<std::shared_ptr<ILifecycleObserver>> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (phase_ != LifecyclePhase::Created) {
            log::error_fmt("LifecycleManager: cannot begin initialization from phase {}",
                           static_cast<int>(phase_));
            return;
        }
        phase_ = LifecyclePhase::Initializing;
        snapshot = observers_;
    }
    notify_observers(snapshot, LifecycleEvent::BeforeInit, component_name);
}

void LifecycleManager::end_initialization(const std::string& component_name) {
    std::vector<std::shared_ptr<ILifecycleObserver>> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (phase_ != LifecyclePhase::Initializing) {
            log::error_fmt("LifecycleManager: cannot end initialization from phase {}",
                           static_cast<int>(phase_));
            return;
        }
        phase_ = LifecyclePhase::Ready;
        snapshot = observers_;
    }
    notify_observers(snapshot, LifecycleEvent::AfterInit, component_name);
    log::info_fmt("LifecycleManager: initialization complete, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::begin_shutdown(const std::string& component_name) {
    std::vector<std::shared_ptr<ILifecycleObserver>> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (phase_ != LifecyclePhase::Ready) {
            log::error_fmt("LifecycleManager: cannot begin shutdown from phase {}",
                           static_cast<int>(phase_));
            return;
        }
        phase_ = LifecyclePhase::ShuttingDown;
        snapshot = observers_;
    }
    notify_observers(snapshot, LifecycleEvent::BeforeShutdown, component_name);
    log::info_fmt("LifecycleManager: shutdown started, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::end_shutdown(const std::string& component_name) {
    std::vector<std::shared_ptr<ILifecycleObserver>> snapshot;
    {
        std::lock_guard lock(mutex_);
        if (phase_ != LifecyclePhase::ShuttingDown) {
            log::error_fmt("LifecycleManager: cannot end shutdown from phase {}",
                           static_cast<int>(phase_));
            return;
        }
        phase_ = LifecyclePhase::Destroyed;
        snapshot = observers_;
    }
    notify_observers(snapshot, LifecycleEvent::AfterShutdown, component_name);
    log::info_fmt("LifecycleManager: shutdown complete, component={}",
                  component_name.empty() ? "runtime" : component_name);
}

void LifecycleManager::force_phase(LifecyclePhase phase) {
    std::lock_guard lock(mutex_);
    log::info_fmt("LifecycleManager: force phase {} -> {}",
                  static_cast<int>(phase_), static_cast<int>(phase));
    phase_ = phase;
}

void LifecycleManager::notify_observers(
        const std::vector<std::shared_ptr<ILifecycleObserver>>& snapshot,
        LifecycleEvent event,
        const std::string& component_name) {
    for (auto& observer : snapshot) {
        try {
            observer->on_lifecycle_event(event, component_name);
        } catch (const std::exception& e) {
            log::error_fmt("LifecycleManager: observer threw exception: {}", e.what());
        }
    }
}

} // namespace ben_gear::agent::runtime
