#pragma once

#include "base/config/settings.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace ben_gear::log {

class Logger;

/// 生成默认日志文件路径：data/logs/bengear.log（FileSink 内部按天滚动）
std::filesystem::path default_log_file();

/// 逗号分隔的 outputs 字符串中是否包含指定 sink 名称（大小写不敏感）
bool wants_sink(std::string_view outputs, std::string_view name);

/// 根据配置创建 Logger（含 stdout / file sink）
std::shared_ptr<Logger> make_logger(const config::Settings& settings);

/// 一次性配置全局日志系统
void configure(const config::Settings& settings);

}  // namespace ben_gear::log
