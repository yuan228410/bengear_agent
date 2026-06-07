#pragma once

#include "ben_gear/base/container/string.hpp"

#include <filesystem>
#include <string_view>
#include <vector>

namespace ben_gear::cli {

namespace container = base::container;

/// 输入历史管理
///
/// 职责：历史记录的增删查 + 持久化
/// 零终端 I/O 依赖，纯数据操作
class HistoryStore {
public:
    /// 最大历史条数
    static constexpr size_t kMaxEntries = 1000;

    /// 从文件加载历史（不存在则创建）
    void load(const std::filesystem::path& path);

    /// 保存历史到文件
    void save(const std::filesystem::path& path) const;

    /// 默认历史文件路径
    static std::filesystem::path default_path();

    /// 添加一条历史（去重：如果与最后一条相同则跳过）
    void add(std::string_view line);

    /// 上一条（↑），返回对应内容
    std::string_view up();

    /// 下一条（↓），返回对应内容
    std::string_view down();

    /// 重置浏览位置（提交后调用）
    void reset_nav();

    /// 当前浏览索引位置对应的内容
    std::string_view current() const;

    /// 是否在浏览模式
    bool browsing() const { return nav_pos_ < entries_.size(); }

    /// 历史条数
    size_t size() const { return entries_.size(); }

private:
    std::vector<container::String> entries_;
    size_t nav_pos_ = 0;   // 0 = 不在浏览，1 = 最新的，size = 最老的
};

}  // namespace ben_gear::cli
