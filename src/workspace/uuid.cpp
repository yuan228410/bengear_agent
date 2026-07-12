#include "workspace/uuid.hpp"
#include "base/platform/random.hpp"

namespace ben_gear::workspace {

container::String generate_uuid() {
    return container::String(base::platform::generate_uuid().c_str());
}

}  // namespace ben_gear::workspace
