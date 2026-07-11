#include "capabilities/capability.hpp"
#include "capabilities/capability_registry.hpp"
#include "workspace/types.hpp"
#include "base/domain/result.hpp"

#include <string>

namespace ben_gear::examples {

class HelloCapability final : public capabilities::CapabilityBase<HelloCapability> {
public:
    static constexpr const char* kName = "hello";

    explicit HelloCapability(workspace::WorkspaceContext ws_ctx)
        : CapabilityBase<HelloCapability>(std::move(ws_ctx)) {}

    domain::AppResult<std::string> greet(const std::string& name) {
        return domain::AppResult<std::string>::success("Hello, " + name + " from plugin!");
    }
};

} // namespace ben_gear::examples

// 插件入口点：程序加载插件时自动调用
extern "C" void ben_gear_plugin_init() {
    BEN_GEAR_REGISTER_CAPABILITY("hello", ben_gear::examples::HelloCapability);
}