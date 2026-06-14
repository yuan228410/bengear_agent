#include "ben_gear/server/http/parser.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ben_gear::server {

/// URL 解码（处理 %XX 转义）
static std::string url_decode(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '%' && i + 2 < input.size()) {
            auto hex = input.substr(i + 1, 2);
            char buf[3] = {static_cast<char>(hex[0]), static_cast<char>(hex[1]), '\0'};
            char* end = nullptr;
            long val = std::strtol(buf, &end, 16);
            if (end == buf + 2) {
                out += static_cast<char>(val);
                i += 2;
                continue;
            }
        } else if (input[i] == '+') {
            out += ' ';
        } else {
            out += input[i];
        }
    }
    return out;
}

HttpRequest parse_http(std::string_view raw) {
    HttpRequest req;
    auto line_end = raw.find("\r\n");
    if (line_end == std::string_view::npos) return req;
    auto line = raw.substr(0, line_end);
    auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) return req;
    req.method = container::String(line.substr(0, sp1));
    auto sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return req;
    auto path_view = line.substr(sp1 + 1, sp2 - sp1 - 1);
    auto qmark = path_view.find('?');
    if (qmark != std::string_view::npos) {
        req.path = container::String(path_view.substr(0, qmark));
        auto qs = path_view.substr(qmark + 1);
        size_t pos = 0;
        while (pos < qs.size()) {
            auto eq = qs.find('=', pos);
            if (eq == std::string_view::npos) break;
            auto amp = qs.find('&', eq + 1);
            auto key = qs.substr(pos, eq - pos);
            auto val = (amp != std::string_view::npos) ? qs.substr(eq + 1, amp - eq - 1) : qs.substr(eq + 1);
            req.query[container::String(url_decode(key))] = container::String(url_decode(val));
            pos = (amp != std::string_view::npos) ? amp + 1 : qs.size();
        }
    } else {
        req.path = container::String(path_view);
    }
    req.version = container::String(line.substr(sp2 + 1));
    auto pos = line_end + 2;
    while (pos < raw.size()) {
        auto header_end = raw.find("\r\n", pos);
        if (header_end == std::string_view::npos) break;
        if (header_end == pos) { req.body = std::string(raw.substr(pos + 2)); break; }
        auto header_line = raw.substr(pos, header_end - pos);
        auto colon = header_line.find(':');
        if (colon != std::string_view::npos) {
            auto key = header_line.substr(0, colon);
            auto val = header_line.substr(colon + 1);
            while (!val.empty() && val[0] == ' ') val.remove_prefix(1);
            std::string key_lower(key);
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            req.headers[key_lower] = std::string(val);
        }
        pos = header_end + 2;
    }
    return req;
}

net::Task<std::string> read_http_request(net::TcpStream& stream) {
    std::string buffer;
    buffer.resize(4096);
    std::string header_part;
    while (true) {
        auto n = co_await stream.read_some(buffer.data(), buffer.size());
        if (n == 0) co_return std::string();
        header_part.append(buffer.data(), n);
        auto pos = header_part.find("\r\n\r\n");
        if (pos != std::string::npos) {
            auto header_size = pos + 4;
            auto body_start = header_part.substr(header_size);
            int content_length = 0;
            auto cl_pos = header_part.find("Content-Length:");
            if (cl_pos == std::string::npos) cl_pos = header_part.find("content-length:");
            if (cl_pos != std::string::npos) {
                auto val_start = header_part.c_str() + cl_pos;
                while (*val_start != ':' && *val_start != '\0') ++val_start;
                ++val_start;
                while (*val_start == ' ') ++val_start;
                content_length = std::atoi(val_start);
            }
            auto remaining = content_length - static_cast<int>(body_start.size());
            while (remaining > 0) {
                auto to_read = std::min(remaining, static_cast<int>(buffer.size()));
                auto n2 = co_await stream.read_some(buffer.data(), to_read);
                if (n2 == 0) break;
                body_start.append(buffer.data(), n2);
                remaining -= static_cast<int>(n2);
            }
            co_return header_part.substr(0, header_size) + body_start;
        }
        if (header_part.size() > 65536) co_return std::string();
    }
}

} // namespace ben_gear::server
