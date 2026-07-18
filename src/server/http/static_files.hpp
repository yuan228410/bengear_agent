#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ben_gear::server {

/// 静态文件服务（内存缓存 + 磁盘读取）
/// 首次访问从磁盘读取并缓存，后续请求直接从内存返回，避免阻塞 event loop
class StaticFileServer {
public:
    struct FileResponse {
        std::string content;
        std::string content_type;
        size_t content_length;
    };

    explicit StaticFileServer(const std::string& root_dir);

    std::optional<FileResponse> serve(const std::string& path) const;
    bool valid() const noexcept { return valid_; }
    const std::string& root_dir() const noexcept { return root_dir_; }

    /// 启动时预热：扫描 dist 目录，将 index.html 和常见资源文件加载到内存缓存
    void warmup();

private:
    std::string root_dir_;
    bool valid_ = false;

    /// 内存缓存：path → (content, content_type)
    mutable std::mutex cache_mutex_;
    mutable std::unordered_map<std::string, FileResponse> cache_;

    std::optional<FileResponse> load_from_disk(const std::string& path) const;
    static std::string guess_content_type(const std::string& path);
};

} // namespace ben_gear::server
