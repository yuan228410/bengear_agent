#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/base/utils/string_utils.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::config {

/// 读取文本文件内容
std::string read_text_file(const std::filesystem::path& path);

/// 去除字符串两端引号
std::string strip_quotes(std::string value);

/// 将 JSON 字段应用到 Settings
void apply_json_to_settings(Settings& settings, const Json& json);

// ==================== model_config 分组配置支持 ====================

/// active_model 引用解析结果
struct ActiveModelRef {
    std::string provider_name;  // provider 名
    std::string model_name;     // model name
};

/// 解析 active_model 字符串，检测 provider:model 新格式
ActiveModelRef parse_active_model_ref(const std::string& active_model);

/// 将 model_config 分组结构展平为平铺 JSON
Json flatten_model_config(const Json& model_config_json, const ActiveModelRef& ref);

// ==================== 模型配置加载 ====================

/// 从 JSON 对象创建 Settings
Settings settings_from_json_model(const Json& model_json);

/// 从 JSON 文件加载模型配置
Settings load_model_config(const std::filesystem::path& path, std::string model_name = {});

/// 列出模型配置文件中所有可用模型
std::vector<std::string> list_models(const std::filesystem::path& path);

/// 加载完整配置（JSON 文件 + 环境变量覆盖）
Settings load_config(const std::filesystem::path& workspace = {},
                     const std::filesystem::path& model_config_path = {},
                     std::string model_name = {});

}  // namespace ben_gear::config

namespace ben_gear {
using config::load_config;
using config::load_model_config;
using config::list_models;
}  // namespace ben_gear
