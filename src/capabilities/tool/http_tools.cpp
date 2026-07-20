#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "net/http.hpp"
#include "net/io_context.hpp"
#include "base/utils/json.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

void register_http_tools(ToolRegistry& registry, net::IoContext& io_ctx) {
    registry.register_tool(
        std::string("http_get"),
        std::string("Perform an HTTP GET request and return the response"),
        {
            {std::string("url"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("URL to fetch")
            }},
            {std::string("headers"), ToolParameterSchema{
                .type = std::string("array"),
                .description = std::string("Optional HTTP headers (array of 'Key: Value' strings)")
            }}
        },
        [&io_ctx](const Json& args) -> std::string {
            std::string url = args.at("url").get<std::string>();
            std::vector<std::string> headers;
            if (args.contains("headers") && args.at("headers").is_array()) {
                for (const auto& h : args.at("headers")) {
                    headers.push_back(h.get<std::string>());
                }
            }
            constexpr int max_retries = 2;
            for (int attempt = 0; attempt <= max_retries; ++attempt) {
                try {
                    net::HttpClient client;
                    auto response = net::sync_wait(io_ctx.loop(),
                        client.get_async(io_ctx.loop(), url, headers));
                    log::debug_fmt("http_get: {} -> status={}", url, response.status);
                    if (response.status == 0) {
                        if (attempt < max_retries) {
                            log::warn_fmt("http_get retry {}/{}: {} - no response", attempt + 1, max_retries, url);
                            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                            continue;
                        }
                        return Json{{"success", false}, {"status", 0}, {"error", "connection failed after retries"}}.dump();
                    }
                    return Json{{"success", true}, {"status", response.status}, {"body", response.body}}.dump();
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    bool transient = err.find("TLS handshake") != std::string::npos ||
                                     err.find("DecryptMessage") != std::string::npos ||
                                     err.find("reset") != std::string::npos ||
                                     err.find("timeout") != std::string::npos ||
                                     err.find("refused") != std::string::npos;
                    if (transient && attempt < max_retries) {
                        log::warn_fmt("http_get retry {}/{}: {} - {}", attempt + 1, max_retries, url, err);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                        continue;
                    }
                    log::error_fmt("http_get failed: {} - {}", url, err);
                    return Json{{"success", false}, {"error", err}}.dump();
                }
            }
            return Json{{"success", false}, {"error", "unreachable"}}.dump();
        }
    );

    registry.register_tool(
        std::string("http_post"),
        std::string("Perform an HTTP POST request with JSON body"),
        {
            {std::string("url"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("URL to post to")
            }},
            {std::string("body"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("JSON request body")
            }},
            {std::string("headers"), ToolParameterSchema{
                .type = std::string("array"),
                .description = std::string("Optional HTTP headers (array of 'Key: Value' strings)")
            }}
        },
        [&io_ctx](const Json& args) -> std::string {
            std::string url = args.at("url").get<std::string>();
            std::string body = args.at("body").get<std::string>();
            std::vector<std::string> headers;
            if (args.contains("headers") && args.at("headers").is_array()) {
                for (const auto& h : args.at("headers")) {
                    headers.push_back(h.get<std::string>());
                }
            }
            constexpr int max_retries = 2;
            for (int attempt = 0; attempt <= max_retries; ++attempt) {
                try {
                    net::HttpClient client;
                    std::vector<std::string> c_headers;
                    for (const auto& h : headers) {
                        c_headers.push_back(std::string(h.data(), h.size()));
                    }
                    auto response = net::sync_wait(io_ctx.loop(),
                        client.post_json_async(io_ctx.loop(),
                            std::string(url.data(), url.size()),
                            std::string(body.data(), body.size()),
                            std::move(c_headers)));
                    log::debug_fmt("http_post: {} -> status={}", url, response.status);
                    if (response.status == 0) {
                        if (attempt < max_retries) {
                            log::warn_fmt("http_post retry {}/{}: {} - no response", attempt + 1, max_retries, url);
                            std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                            continue;
                        }
                        return Json{{"success", false}, {"status", 0}, {"error", "connection failed after retries"}}.dump();
                    }
                    return Json{{"success", true}, {"status", response.status}, {"body", response.body}}.dump();
                } catch (const std::exception& e) {
                    std::string err = e.what();
                    bool transient = err.find("TLS handshake") != std::string::npos ||
                                     err.find("DecryptMessage") != std::string::npos ||
                                     err.find("reset") != std::string::npos ||
                                     err.find("timeout") != std::string::npos ||
                                     err.find("refused") != std::string::npos;
                    if (transient && attempt < max_retries) {
                        log::warn_fmt("http_post retry {}/{}: {} - {}", attempt + 1, max_retries, url, err);
                        std::this_thread::sleep_for(std::chrono::milliseconds(500 * (attempt + 1)));
                        continue;
                    }
                    log::error_fmt("http_post failed: {} - {}", url, err);
                    return Json{{"success", false}, {"error", err}}.dump();
                }
            }
            return Json{{"success", false}, {"error", "unreachable"}}.dump();
        }
    );
}

} // namespace ben_gear::tools
