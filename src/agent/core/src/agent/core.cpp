#pragma once

#include "agent/core/interface/agent_core_interfaces.hpp"

namespace ben_gear::agent::core {

// Event system implementation
class EventSystemImpl : public IEventSystem {
public:
    EventSystemImpl();
    ~EventSystemImpl() override;

    void publish(const std::shared_ptr<IEvent>& event) override;
    void publish(EventType type, const std::string& source,
                 const std::string& target, const Json& data) override;

    std::string subscribe(EventType type,
                          std::function<void(const std::shared_ptr<IEvent>&)> callback) override;
    void unsubscribe(const std::string& subscription_id) override;

    void add_filter(std::function<bool(const std::shared_ptr<IEvent>&)> filter) override;
    void remove_filter(std::function<bool(const std::shared_ptr<IEvent>&)> filter) override;

    std::vector<std::shared_ptr<IEvent>> get_events(EventType type,
                                                    const std::chrono::system_clock::time_point& from,
                                                    const std::chrono::system_clock::time_point& to) override;

    std::vector<std::shared_ptr<IEvent>> query(const EventQuery& query) override;

private:
    std::unordered_map<EventType,
                       std::vector<std::pair<std::string,
                                             std::function<void(const std::shared_ptr<IEvent>&)>>>> subscriptions_;
    std::vector<std::function<bool(const std::shared_ptr<IEvent>&)>> filters_;
    std::vector<std::shared_ptr<IEvent>> event_history_;
    std::mutex mutex_;
    std::atomic<int64_t> event_counter_{0};
};

// Plugin base class implementation
class AgentPlugin : public IAgentPlugin {
public:
    virtual ~AgentPlugin() = default;

    std::string name() const override { return "BasePlugin"; }
    std::string version() const override { return "1.0.0"; }
    std::string description() const override { return "Base plugin implementation"; }
    PluginType plugin_type() const override { return PluginType::UTILITY; }
    std::vector<std::string> capabilities() const override { return {}; }

    bool initialize(const std::any& config, IPluginRegistry& registry) override {
        registry_ = std::make_shared<std::reference_wrapper<IPluginRegistry>>(registry);
        return on_initialize(config, registry);
    }

    void on_initialized() override {}
    void on_shutdown() override {}
    void pause() override {}
    void resume() override {}

    bool execute(const std::string& input, std::string& output) override {
        return on_execute(input, output);
    }

protected:
    virtual bool on_initialize(const std::any& config, IPluginRegistry& registry) {
        (void)config;
        (void)registry;
        return true;
    }

    virtual bool on_execute(const std::string& input, std::string& output) {
        output = "Plugin executed: " + input;
        return true;
    }

    template<typename ServiceType>
    std::shared_ptr<ServiceType> get_service(IPluginRegistry& registry) {
        return registry.get_service<ServiceType>();
    }

    void log(const std::string& message, LogLevel level = LogLevel::INFO) {
        // Implementation would use actual logging system
    }

private:
    PluginState state_{PluginState::UNLOADED};
    std::weak_ptr<IPluginRegistry> registry_;
    std::shared_ptr<spdlog::logger> logger_;
};

// Service registry implementation
class ServiceRegistryImpl : public IPluginRegistry {
public:
    ~ServiceRegistryImpl() override = default;

    template<typename ServiceType>
    void register_service(std::shared_ptr<ServiceType> service) {
        services_[typeid(ServiceType)] = service;
    }

    template<typename ServiceType>
    std::shared_ptr<ServiceType> get_service() const {
        auto it = services_.find(typeid(ServiceType));
        if (it != services_.end()) {
            return std::static_pointer_cast<ServiceType>(it->second);
        }
        return nullptr;
    }

    void register_service_factory(ServiceFactory<ServiceType> factory) {
        service_factories_[typeid(ServiceType)] = [factory]() -> std::shared_ptr<void> {
            return factory();
        };
    }

    void load_plugin(const std::filesystem::path& plugin_path,
                     const std::any& config) override;
    void unload_plugin(const std::string& plugin_name) override;
    bool is_plugin_loaded(const std::string& plugin_name) const override;

    std::vector<PluginInfo> list_plugins() const override;
    PluginInfo get_plugin_info(const std::string& plugin_name) const override;

    void enable_plugin(const std::string& plugin_name) override;
    void disable_plugin(const std::string& plugin_name) override;

protected:
    void register_plugin_object(const std::string& name,
                               std::shared_ptr<IAgentPlugin> plugin) override;

private:
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
    std::unordered_map<std::type_index, std::function<std::shared_ptr<void>()>> service_factories_;
    std::unordered_map<std::string, std::shared_ptr<IAgentPlugin>> plugins_;
    std::unordered_map<std::string, PluginState> plugin_states_;
};

} // namespace ben_gear::agent::core
