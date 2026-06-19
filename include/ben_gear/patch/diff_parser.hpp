#pragma once

#include "ben_gear/patch/types.hpp"

#include <string_view>

namespace ben_gear::patch {

PatchPreview parse_unified_diff(std::string_view unified_diff);
PatchPreview empty_patch_preview();

} // namespace ben_gear::patch
