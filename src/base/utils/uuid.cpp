#include "base/utils/uuid.hpp"
#include "platform/random.hpp"

namespace ben_gear::base::utils {

std::string generate_uuid() {
    return base::platform::generate_uuid();
}

}  // namespace ben_gear::base::utils
