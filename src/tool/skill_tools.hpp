#pragma once

#include "llm/skill/skill.hpp"
#include "llm/skill/zip_extract.hpp"
#include "tool/builtin_tools.hpp"
#include "tool/registry.hpp"
#include "base/log/logger.hpp"
#include "base/net/io_context.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::llm;
using SkillDefinition = ben_gear::skill::SkillDefinition;
using SkillLoader = ben_gear::skill::SkillLoader;
using skill::download_file;
using skill::extract_zip;

/// 获取内置技能定义列表
base::container::Vector<SkillDefinition> builtin_skill_definitions();


/// 注册 get_skill 工具（Level 2 按需加载）
void register_skill_tools(ToolRegistry& registry, SkillLoader* loader);


/// 生成临时目录路径
std::string make_temp_dir();


/// 注册技能管理工具（install, remove, enable, disable, list）
void register_skill_management_tools(ToolRegistry& registry,
                                             SkillLoader* loader,
                                             net::IoContext& io_ctx);


/// 注册所有工具的总入口（内置工具 + 技能工具 + 技能管理工具）
void register_all_tools(ToolRegistry& registry, int command_timeout,
                                     SkillLoader* loader, net::IoContext& io_ctx);


}  // namespace ben_gear::tools
