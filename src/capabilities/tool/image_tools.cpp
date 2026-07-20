#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "base/utils/json.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_image_tools(ToolRegistry& registry) {
    registry.register_tool(
        std::string("read_image"),
        std::string("Read an image file and return base64-encoded content with metadata. "
            "Supports PNG, JPEG, GIF, WebP, BMP formats."),
        {{std::string("path"), {std::string("string"), std::string("Image file path")}}},
        [](const Json& args) -> std::string {
            std::string path = args.at("path").get<std::string>();
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file) {
                return Json{{"success", false}, {"error", "Cannot open: " + path}}.dump();
            }
            auto size = static_cast<size_t>(file.tellg());
            file.seekg(0);
            std::string data(size, '\0');
            file.read(data.data(), static_cast<std::streamsize>(size));

            auto ext = std::filesystem::path(path).extension().string();
            std::string mime = "image/png";
            if (ext == ".jpg" || ext == ".jpeg") mime = "image/jpeg";
            else if (ext == ".gif") mime = "image/gif";
            else if (ext == ".webp") mime = "image/webp";
            else if (ext == ".bmp") mime = "image/bmp";

            static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string b64;
            b64.reserve(((size + 2) / 3) * 4);
            for (size_t i = 0; i < size; i += 3) {
                uint32_t n = static_cast<uint8_t>(data[i]) << 16;
                if (i + 1 < size) n |= static_cast<uint8_t>(data[i + 1]) << 8;
                if (i + 2 < size) n |= static_cast<uint8_t>(data[i + 2]);
                b64 += chars[(n >> 18) & 63];
                b64 += chars[(n >> 12) & 63];
                b64 += (i + 1 < size) ? chars[(n >> 6) & 63] : '=';
                b64 += (i + 2 < size) ? chars[n & 63] : '=';
            }

            log::debug_fmt("read_image: {} ({} bytes, {})", path, size, mime);
            return std::string(Json{{"success", true}, {"path", path},
                {"size", static_cast<int64_t>(size)}, {"mime_type", mime},
                {"data", "data:" + mime + ";base64," + b64}}.dump().c_str());
        }
    );
}

} // namespace ben_gear::tools
