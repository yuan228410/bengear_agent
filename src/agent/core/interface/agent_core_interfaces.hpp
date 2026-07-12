// ----------------------------------------------------------------------------
// Core Agent Interfaces - Minimal Agent Core Components
// ----------------------------------------------------------------------------

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <any>
#include <functional>
#include <atomic>
#include <chrono>

namespace ben_gear::agent::core {

// Forward declarations
class IEventSystem;
class IPluginRegistry;
class IFileService;
class IWebAccessService;
class ISkillService;
class ICommandExecutor;
class IMCPService;

// Core exceptions
class AgentException : public std::runtime_error {
public:
    explicit AgentException(const std::string& message)
        : std::runtime_error(message) {}
};

class PluginException : public std::runtime_error {
public:
    explicit PluginException(const std::string& message)
        : std::runtime_error(message) {}
};

// Event system types
enum class EventType {
    PLUGIN_LOADED,
    PLUGIN_UNLOADED,
    PLUGIN_ERROR,
    SYSTEM_SHUTDOWN,
    SESSION_CREATED,
    MESSAGE_STORED,
    TOOL_EXECUTED,
    CUSTOM
};

class IEvent {
public:
    virtual ~IEvent() = default;
    virtual EventType type() const = 0;
    virtual int64_t timestamp() const = 0;
    virtual const std::string& source() const = 0;
    virtual const std::string& target() const = 0;
    virtual const Json& data() const = 0;
};

// Plugin registration info
struct PluginInfo {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    PluginType type;
    std::vector<std::string> dependencies;
    std::chrono::system_clock::time_point timestamp;
};

// Plugin type enumeration
enum class PluginType {
    BUILTIN,    // Core functionality
    SYSTEM,     // System-level plugins
    INTEGRATION, // Third-party integrations
    UTILITY     // Utility plugins
};

// Plugin base interface
class IAgentPlugin {
public:
    virtual ~IAgentPlugin() = default;
    
    // Plugin identification
    virtual std::string name() const = 0;
    virtual std::string version() const = 0;
    virtual std::string description() const = 0;
    virtual PluginType plugin_type() const = 0;
    virtual std::vector<std::string> capabilities() const = 0;
    
    // Lifecycle management
    virtual bool initialize(const std::any& config, IPluginRegistry& registry) {
        return on_initialize(config, registry);
    }
    
    virtual void on_initialized() {}
    virtual void on_shutdown() {}
    virtual void pause() {}
    virtual void resume() {}
    
    // Event handling
    virtual void on_event(const std::shared_ptr<IEvent>& event) {}
    
    // Core functionality
    virtual bool execute(const std::string& input, std::string& output) {
        return on_execute(input, output);
    }
    
protected:
    virtual bool on_initialize(const std::any& config, IPluginRegistry& registry) = 0;
    virtual bool on_execute(const std::string& input, std::string& output) = 0;
    
    // Helper methods
    template<typename ServiceType>
    std::shared_ptr<ServiceType> get_service(IPluginRegistry& registry);
    
    void log(const std::string& message, LogLevel level = LogLevel::INFO);
    
private:
    PluginState state_{PluginState::UNLOADED};
    std::weak_ptr<IPluginRegistry> registry_;
    std::shared_ptr<spdlog::logger> logger_;
};

// Service registry interface
class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;
    
    // Plugin management
    virtual std::string load_plugin(const std::filesystem::path& plugin_path, 
                                   const std::any& config) = 0;
    
    virtual bool unload_plugin(const std::string& plugin_name) = 0;
    virtual bool is_plugin_loaded(const std::string& plugin_name) const = 0;
    
    // Plugin querying
    virtual std::vector<PluginInfo> list_plugins() const = 0;
    virtual PluginInfo get_plugin_info(const std::string& plugin_name) const = 0;
    
    // Plugin lifecycle management
    virtual void enable_plugin(const std::string& plugin_name) = 0;
    virtual void disable_plugin(const std::string& plugin_name) = 0;
    
    // Service access
    template<typename ServiceType>
    std::shared_ptr<ServiceType> get_service() const;
    
protected:
    virtual void register_plugin_object(const std::string& name, 
                                       std::shared_ptr<IAgentPlugin> plugin) = 0;
};

// Core services interfaces
class IFileService {
public:
    virtual ~IFileService() = default;
    
    // File operations
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual std::string read_text(const std::filesystem::path& path) const = 0;
    virtual bool write_text(const std::filesystem::path& path, const std::string& content) = 0;
    virtual bool remove(const std::filesystem::path& path) = 0;
    virtual bool create_directory(const std::filesystem::path& path) = 0;
    virtual std::vector<std::string> list_files(const std::filesystem::path& path) const = 0;
    
    // File info
    virtual std::filesystem::path canonical_path(const std::filesystem::path& path) const = 0;
    virtual uint64_t file_size(const std::filesystem::path& path) const = 0;
    virtual std::chrono::system_clock::time_point last_modified(const std::filesystem::path& path) const = 0;
    
    // Utility operations
    virtual bool copy(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual bool move(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual bool replace(const std::filesystem::path& path, const std::string& content) = 0;
    
    // Security and validation
    virtual bool is_safe_path(const std::filesystem::path& path, const std::string& base_dir) const = 0;
    virtual std::filesystem::path secure_path(const std::filesystem::path& path, const std::string& base_dir) const = 0;
};

class IWebAccessService {
public:
    virtual ~IWebAccessService() = default;
    
    // HTTP operations
    virtual HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {}) = 0;
    virtual HttpResponse post(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {}) = 0;
    virtual HttpResponse put(const std::string& url, const std::string& body, const std::map<std::string, std::string>& headers = {}) = 0;
    virtual HttpResponse delete(const std::string& url, const std::map<std::string, std::string>& headers = {}) = 0;
    
    // Web utilities
    virtual std::string url_encode(const std::string& value) = 0;
    virtual std::string url_decode(const std::string& encoded) = 0;
    virtual std::string extract_domain(const std::string& url) = 0;
    
    // WebSocket operations
    virtual void connect_websocket(const std::string& url, std::function<void(const std::string&)> callback) = 0;
    virtual void disconnect_websocket(const std::string& url) = 0;
    virtual void send_websocket_message(const std::string& url, const std::string& message) = 0;
    
protected:
    virtual void configure_tls_context(void* context) = 0;
    virtual void set_proxy(const std::string& host, int port) = 0;
};

class ISkillService {
public:
    virtual ~ISkillService() = default;
    
    // Skill management
    virtual void register_skill(const SkillDefinition& skill) = 0;
    virtual bool unregister_skill(const std::string& skill_name) = 0;
    virtual std::optional<SkillDefinition> get_skill(const std::string& skill_name) const = 0;
    virtual std::vector<SkillDefinition> list_skills() const = 0;
    
    // Skill execution
    virtual std::string execute_skill(const std::string& skill_name, 
                                     const std::map<std::string, std::string>& parameters,
                                     const SkillExecutionContext& context) = 0;
    
    // Skill categories
    virtual std::vector<std::string> get_skill_categories() const = 0;
    virtual std::vector<SkillDefinition> get_skills_by_category(const std::string& category) const = 0;
    
    // Skill documentation
    virtual std::string get_skill_documentation(const std::string& skill_name) const = 0;
    virtual std::map<std::string, std::string> get_skill_metadata(const std::string& skill_name) const = 0;
    
    // Skill validation
    virtual bool validate_skill_parameters(const std::string& skill_name, 
                                          const std::map<std::string, std::string>& parameters) const = 0;
    
protected:
    virtual void load_skills_from_file(const std::string& file_path) = 0;
    virtual void save_skill_to_file(const SkillDefinition& skill, const std::string& file_path) = 0;
};

class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;
    
    // Command execution
    virtual CommandResult execute(const std::string& command,
                                  const std::vector<std::string>& args = {},
                                  const std::map<std::string, std::string>& env = {},
                                  const std::string& working_directory = "") = 0;
    
    // Command pipeline
    virtual std::vector<CommandResult> execute_pipeline(const std::vector<std::string>& commands) = 0;
    
    // Script execution
    virtual CommandResult execute_script(const std::string& script_path,
                                         const std::map<std::string, std::string>& variables = {}) = 0;
    
    // Process management
    virtual bool is_process_running(const std::string& process_id) = 0;
    virtual bool terminate_process(const std::string& process_id, int signal = 9) = 0;
    
    // Command history
    virtual void log_command(const std::string& command, const CommandResult& result) = 0;
    virtual std_support::Json get_command_history(size_t limit = 100) const = 0;
    virtual void clear_command_history() = 0;
    
    // Security validation
    virtual bool validate_command(const std::string& command,
                                 const std::vector<std::string>& args) const = 0;
    virtual bool is_safe_command(const std::string& command) const = 0;
    
protected:
    virtual void setup_process_environment(std::chrono::system_clock::time_point start_time) = 0;
    virtual void cleanup_process_environment() = 0;
    virtual void configure_output_handling(void* stdout_pipe, void* stderr_pipe) = 0;
    virtual void set_command_timeout(double timeout_seconds) = 0;
    virtual void set_retry_policy(int max_retries, double retry_delay) = 0;
};

class IMCPService {
public:
    virtual ~IMCPService() = default;
    
    // MCP server management
    virtual void connect_server(const std::string& server_name, const MCPServerInfo& server_info) = 0;
    virtual bool disconnect_server(const std::string& server_name) = 0;
    virtual bool is_server_connected(const std::string& server_name) const = 0;
    virtual std::vector<std::string> list_connected_servers() const = 0;
    
    // Tool management
    virtual void register_tool(const std::string& server_name, const MCPTool& tool) = 0;
    virtual bool unregister_tool(const std::string& server_name, const std::string& tool_name) = 0;
    virtual std::optional<MCPTool> get_tool(const std::string& server_name, const std::string& tool_name) const = 0;
    virtual std::vector<MCPTool> list_tools(const std::string& server_name) const = 0;
    
    // Resource management
    virtual void register_resource(const std::string& server_name, const MCPResource& resource) = 0;
    virtual bool unregister_resource(const std::string& server_name, const std::string& resource_uri) = 0;
    virtual std::optional<MCPResource> get_resource(const std::string& server_name, const std::string& resource_uri) const = 0;
    virtual std::vector<MCPResource> list_resources(const std::string& server_name) const = 0;
    
    // Tool calls
    virtual std::string call_tool(const std::string& server_name, const std::string& tool_name,
                                 const std::map<std::string, std::string>& parameters) = 0;
    
    // Prompts
    virtual std::vector<std::string> list_prompts(const std::string& server_name) const = 0;
    virtual std::string get_prompt(const std::string& server_name, const std::string& prompt_name,
                                   const std::map<std::string, std::string>& arguments) = 0;
    
    // Event handling
    virtual void add_event_handler(EventType event_type,
                                   std::function<void(const MCPEvent&)> handler) = 0;
    virtual void remove_event_handler(EventType event_type) = 0;
    
protected:
    virtual void setup_connection(const MCPServerInfo& server_info) = 0;
    virtual void teardown_connection(const std::string& server_name) = 0;
    virtual bool authenticate(const std::string& server_name,
                              const std::map<std::string, std::string>& credentials) = 0;
};

} // namespace ben_gear::agent::core
