#pragma once

#include "capabilities/skill/skill.hpp"
#include "capabilities/skill/zip_extract.hpp"
#include "capabilities/tool/registry.hpp"

// 前向声明
namespace ben_gear::net { class TlsEngine; class IoContext; }
namespace ben_gear::compress { class CompressEngine; }

namespace ben_gear::skill {

/// 注册 get_skill 工具（Level 2 按需加载）
void register_skill_tools(capabilities::tool::ToolRegistry& registry, SkillLoader* loader);

/// 生成临时目录路径
std::string make_temp_dir();

/// 注册技能管理工具（install, remove, enable, disable, list）
void register_skill_management_tools(capabilities::tool::ToolRegistry& registry,
                                     SkillLoader* loader,
                                     net::IoContext& io_ctx,
                                     net::TlsEngine& tls_engine,
                                     compress::CompressEngine& compress_engine);

/// 注册技能工具的总入口（技能工具 + 技能管理工具）
/// 内置工具由运行时单独注册
void register_all_tools(capabilities::tool::ToolRegistry& registry, int command_timeout,
                        SkillLoader* loader, net::IoContext& io_ctx,
                        net::TlsEngine& tls_engine,
                        compress::CompressEngine& compress_engine);

}  // namespace ben_gear::skill
