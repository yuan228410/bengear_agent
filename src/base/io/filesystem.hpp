#pragma once

#include "base/utils/json.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ben_gear::base::io {

/// 文件系统操作结果
struct FileResult {
    bool ok = true;
    std::string error;
    std::string content;

    static FileResult success(std::string data = {}) {
        return {true, {}, std::move(data)};
    }
    static FileResult failure(std::string_view what) {
        return {false, std::string(what), {}};
    }
};

/// 文件系统抽象接口 — 支持依赖注入和测试模拟
///
/// 职责：
/// - 封装所有文件系统操作，使工具层可单元测试
/// - 提供 mock 实现用于测试
/// - 隔离平台相关细节
class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    // ==================== 读操作 ====================

    /// 读取文件全部内容
    virtual FileResult read_file(const std::filesystem::path& path) = 0;

    /// 读取文件二进制内容
    virtual std::vector<uint8_t> read_binary(const std::filesystem::path& path) = 0;

    /// 检查路径是否存在
    virtual bool exists(const std::filesystem::path& path) = 0;

    /// 获取文件大小（字节）
    virtual uint64_t file_size(const std::filesystem::path& path) = 0;

    /// 检查路径是否为目录
    virtual bool is_directory(const std::filesystem::path& path) = 0;

    /// 检查路径是否为普通文件
    virtual bool is_regular_file(const std::filesystem::path& path) = 0;

    /// 列出目录内容（非递归）
    virtual std::vector<std::filesystem::path> list_directory(
        const std::filesystem::path& dir) = 0;

    /// 递归列出目录内容
    virtual std::vector<std::filesystem::path> list_directory_recursive(
        const std::filesystem::path& dir) = 0;

    // ==================== 写操作 ====================

    /// 写入文件（覆盖）
    virtual FileResult write_file(const std::filesystem::path& path,
                                  std::string_view content) = 0;

    /// 追加写入文件
    virtual FileResult append_file(const std::filesystem::path& path,
                                   std::string_view content) = 0;

    /// 创建目录（递归）
    virtual FileResult create_directories(const std::filesystem::path& path) = 0;

    /// 删除文件
    virtual FileResult remove(const std::filesystem::path& path) = 0;

    /// 递归删除目录
    virtual FileResult remove_all(const std::filesystem::path& path) = 0;

    /// 重命名/移动
    virtual FileResult rename(const std::filesystem::path& from,
                              const std::filesystem::path& to) = 0;

    /// 复制文件
    virtual FileResult copy_file(const std::filesystem::path& from,
                                 const std::filesystem::path& to) = 0;

    // ==================== 路径操作 ====================

    /// 获取当前工作目录
    virtual std::filesystem::path current_path() = 0;

    /// 获取文件扩展名
    virtual std::filesystem::path extension(const std::filesystem::path& path) = 0;

    /// 获取父路径
    virtual std::filesystem::path parent_path(const std::filesystem::path& path) = 0;

    /// 路径规范化
    virtual std::filesystem::path weakly_canonical(const std::filesystem::path& path) = 0;

    /// 计算相对路径
    virtual std::filesystem::path relative(const std::filesystem::path& path,
                                           const std::filesystem::path& base) = 0;
};

/// 真实文件系统实现 — 包装 std::filesystem
class RealFileSystem : public IFileSystem {
public:
    FileResult read_file(const std::filesystem::path& path) override;
    std::vector<uint8_t> read_binary(const std::filesystem::path& path) override;
    bool exists(const std::filesystem::path& path) override;
    uint64_t file_size(const std::filesystem::path& path) override;
    bool is_directory(const std::filesystem::path& path) override;
    bool is_regular_file(const std::filesystem::path& path) override;
    std::vector<std::filesystem::path> list_directory(
        const std::filesystem::path& dir) override;
    std::vector<std::filesystem::path> list_directory_recursive(
        const std::filesystem::path& dir) override;

    FileResult write_file(const std::filesystem::path& path,
                          std::string_view content) override;
    FileResult append_file(const std::filesystem::path& path,
                           std::string_view content) override;
    FileResult create_directories(const std::filesystem::path& path) override;
    FileResult remove(const std::filesystem::path& path) override;
    FileResult remove_all(const std::filesystem::path& path) override;
    FileResult rename(const std::filesystem::path& from,
                      const std::filesystem::path& to) override;
    FileResult copy_file(const std::filesystem::path& from,
                         const std::filesystem::path& to) override;

    std::filesystem::path current_path() override;
    std::filesystem::path extension(const std::filesystem::path& path) override;
    std::filesystem::path parent_path(const std::filesystem::path& path) override;
    std::filesystem::path weakly_canonical(const std::filesystem::path& path) override;
    std::filesystem::path relative(const std::filesystem::path& path,
                                   const std::filesystem::path& base) override;
};

} // namespace ben_gear::base::io
