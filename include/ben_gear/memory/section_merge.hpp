#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级 section 合并算法
/// 按 ## 标题拆分 markdown，同名 section 后者优先（last-wins），
/// 但保留首次出现的顺序位置
container::String merge_sections(
    const container::Vector<container::String>& texts);

}  // namespace ben_gear::memory
