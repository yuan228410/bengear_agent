#include "workspace/uuid.hpp"
#include "base/platform/random.hpp"

namespace ben_gear::workspace {

std::string generate_uuid() {
    return base::platform::generate_uuid();
}

}  // namespace ben_gear::workspace
