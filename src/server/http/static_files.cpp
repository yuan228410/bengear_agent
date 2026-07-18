#include "server/http/static_files.hpp"
#include <unordered_map>
#include "base/log/logger.hpp"
#include <fstream>
#include <filesystem>

namespace ben_gear::server {

StaticFileServer::StaticFileServer(const std::string& root_dir) : root_dir_(root_dir) {
    valid_ = std::filesystem::is_directory(root_dir_);
    if(valid_) log::info_fmt("StaticFileServer: serving from {}",root_dir_);
    else log::warn_fmt("StaticFileServer: dir not found: {}",root_dir_);
}

std::optional<StaticFileServer::FileResponse> StaticFileServer::serve(const std::string& path) const {
    if(!valid_) return std::nullopt;
    try {
        auto root = std::filesystem::weakly_canonical(root_dir_);
        auto resolved = std::filesystem::weakly_canonical(root_dir_ + path);
        // 安全校验：确保解析后的路径仍在 root 目录下
        auto rel = std::filesystem::relative(resolved, root);
        auto rel_str = rel.string();
        if(rel_str.empty() || rel_str[0] == '.') return std::nullopt;
        if(std::filesystem::is_directory(resolved)) resolved /= "index.html";
        if(!std::filesystem::exists(resolved)) return std::nullopt;
        // 使用 file_size 替代 tellg，避免 seekg/tellg 失败时 -1 转 SIZE_MAX 导致 OOM
        auto fsize = std::filesystem::file_size(resolved);
        if(fsize > 64 * 1024 * 1024) return std::nullopt; // 拒绝 >64MB 的文件
        std::ifstream file(resolved, std::ios::binary);
        if(!file) return std::nullopt;
        FileResponse resp;
        resp.content_length = static_cast<size_t>(fsize);
        resp.content.resize(resp.content_length);
        file.read(resp.content.data(), static_cast<std::streamsize>(resp.content_length));
        resp.content_type = guess_content_type(resolved.string());
        return resp;
    } catch(const std::exception&) {
        return std::nullopt;
    }
}

std::string StaticFileServer::guess_content_type(const std::string& path) {
    auto dot = path.rfind('.');
    if(dot==std::string::npos) return "application/octet-stream";
    auto ext = path.substr(dot);
    static const std::unordered_map<std::string,std::string> types={
        {".html","text/html; charset=utf-8"},{".htm","text/html; charset=utf-8"},
        {".css","text/css; charset=utf-8"},{".js","application/javascript; charset=utf-8"},
        {".mjs","application/javascript; charset=utf-8"},{".json","application/json; charset=utf-8"},
        {".png","image/png"},{".jpg","image/jpeg"},{".jpeg","image/jpeg"},
        {".gif","image/gif"},{".svg","image/svg+xml"},{".ico","image/x-icon"},
        {".woff","font/woff"},{".woff2","font/woff2"},{".ttf","font/ttf"},
        {".webp","image/webp"},{".map","application/json"},{".wasm","application/wasm"},
    };
    auto it=types.find(ext);
    return (it!=types.end())?it->second:"application/octet-stream";
}

} // namespace ben_gear::server
