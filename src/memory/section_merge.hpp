#pragma once

#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级 section 合并算法
/// 按 ## 标题拆分 markdown，同名 section 后者优先（last-wins），
/// 但保留首次出现的顺序位置
std::string merge_sections(
    const std::vector<std::string>& texts);

}  // namespace ben_gear::memory
