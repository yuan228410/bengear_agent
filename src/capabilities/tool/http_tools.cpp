#include "capabilities/tool/builtin_tools.hpp"

#include "log/logger.hpp"
#include "net/http.hpp"
#include "net/io_context.hpp"
#include "base/utils/json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace ben_gear::tools {

using namespace ben_gear::capabilities::tool;

namespace {

/// 解析 URL 的 scheme 部分
std::string_view url_scheme(std::string_view url) {
    auto pos = url.find("://");
    if (pos == std::string_view::npos) return {};
    return url.substr(0, pos);
}

/// 解析 URL 的 host[:port] 部分（含 scheme）
std::string_view url_authority(std::string_view url) {
    auto start = url.find("://");
    if (start == std::string_view::npos) return {};
    start += 3;
    auto end = url.find('/', start);
    if (end == std::string_view::npos) end = url.size();
    return url.substr(0, end);
}

/// 拼接重定向 URL：处理相对路径、协议相对 URL
std::string resolve_redirect(const std::string& original_url, std::string_view location) {
    if (location.empty()) return original_url;

    // 已经是绝对 URL（含 scheme）
    if (location.find("://") != std::string_view::npos) {
        return std::string(location);
    }

    // 协议相对 URL（以 // 开头）
    if (location.size() >= 2 && location[0] == '/' && location[1] == '/') {
        auto scheme = url_scheme(original_url);
        std::string result(scheme);
        result.append(location);
        return result;
    }

    // 绝对路径重定向（以 / 开头）
    if (!location.empty() && location[0] == '/') {
        auto authority = url_authority(original_url);
        return std::string(authority) + std::string(location);
    }

    // 相对路径重定向：替换原 URL 最后一段路径
    auto query_pos = original_url.find('?');
    auto base = (query_pos != std::string::npos) ? original_url.substr(0, query_pos) : original_url;
    auto last_slash = base.rfind('/');
    if (last_slash != std::string::npos && last_slash > original_url.find("://") + 2) {
        return base.substr(0, last_slash + 1) + std::string(location);
    }
    return original_url + "/" + std::string(location);
}

/// 执行 HTTP 请求并自动跟随重定向（最多 max_redirects 次）
net::HttpResponse http_request_with_redirect(
    net::EventLoop& loop,
    net::TlsEngine& tls_engine,
    const std::string& method,
    std::string url,
    const std::vector<std::string>& headers,
    const std::string& body,
    int max_redirects = 5) {

    net::HttpClient client(net::ConnectionPoolConfig{}, tls_engine);
    std::string current_url = url;
    std::vector<std::string> req_headers = headers;

    for (int redirect = 0; redirect <= max_redirects; ++redirect) {
        net::HttpResponse response;
        if (method == "POST") {
            response = net::sync_wait(loop,
                client.post_json_async(loop, current_url, body, req_headers));
            // POST 重定向时降级为 GET（除 307/308 外）
            if ((response.status == 301 || response.status == 302 || response.status == 303) &&
                redirect < max_redirects) {
                auto loc_it = response.headers.find("location");
                if (loc_it != response.headers.end() && !loc_it->second.empty()) {
                    auto next_url = resolve_redirect(current_url, loc_it->second);
                    log::debug_fmt("http_post: {} -> {} redirect to {}", current_url, response.status, next_url);
                    current_url = next_url;
                    // 切换为 GET
                    response = net::sync_wait(loop,
                        client.get_async(loop, current_url, req_headers));
                    // 如果 GET 也是重定向，继续跟随
                    auto is_redirect = response.status == 301 || response.status == 302 ||
                                       response.status == 303 || response.status == 307 ||
                                       response.status == 308;
                    if (is_redirect && redirect < max_redirects - 1) {
                        auto loc2 = response.headers.find("location");
                        if (loc2 != response.headers.end() && !loc2->second.empty()) {
                            current_url = resolve_redirect(current_url, loc2->second);
                            log::debug_fmt("http_post: {} -> {} redirect to {}", current_url, response.status, current_url);
                            ++redirect;
                            continue;
                        }
                    }
                    return response;
                }
            }
        } else {
            response = net::sync_wait(loop,
                client.get_async(loop, current_url, req_headers));
        }

        log::debug_fmt("{}: {} -> status={}",
                       method == "POST" ? "http_post" : "http_get", current_url, response.status);

        // 检查是否是重定向
        bool is_redirect = (method != "POST") &&
            (response.status == 301 || response.status == 302 ||
             response.status == 303 || response.status == 307 ||
             response.status == 308);

        if (!is_redirect || redirect >= max_redirects) {
            return response;
        }

        auto loc_it = response.headers.find("location");
        if (loc_it == response.headers.end() || loc_it->second.empty()) {
            return response;  // 没有 Location 头，中止
        }

        auto next_url = resolve_redirect(current_url, loc_it->second);
        log::debug_fmt("http_get: {} -> {} redirect to {}", current_url, response.status, next_url);
        current_url = next_url;
    }

    // 超出重定向次数，返回最后的错误响应
    return net::sync_wait(loop, client.get_async(loop, current_url, req_headers));
}

} // anonymous namespace

void register_http_tools(ToolRegistry& registry, net::IoContext& io_ctx, net::TlsEngine& tls_engine) {
    registry.register_tool(
        std::string("http_get"),
        std::string("Perform an HTTP GET request and return the response. "
            "Automatically follows redirects (up to 5 hops)."),
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
        [&io_ctx, &tls_engine](const Json& args) -> std::string {
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
                    auto response = http_request_with_redirect(
                        io_ctx.loop(), tls_engine, "GET", url, headers, "", 5);
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
        std::string("Perform an HTTP POST request with JSON body. "
            "Automatically follows redirects (up to 5 hops)."),
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
        [&io_ctx, &tls_engine](const Json& args) -> std::string {
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
                    auto response = http_request_with_redirect(
                        io_ctx.loop(), tls_engine, "POST", url, headers, body, 5);
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
