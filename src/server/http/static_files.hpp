#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ben_gear::server {

/// 静态文件服务（从磁盘读取，可扩展为嵌入式）
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

private:
    std::string root_dir_;
    bool valid_ = false;
    static std::string guess_content_type(const std::string& path);
};

} // namespace ben_gear::server
