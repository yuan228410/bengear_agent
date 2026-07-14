#include "base/net/http.hpp"

#include "base/utils/string_utils.hpp"

#include <cctype>
#include <stdexcept>

namespace ben_gear::net {

namespace container = base::container;

HttpClient::ParsedUrl HttpClient::parse_url(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        throw std::runtime_error("url missing scheme");
    }
    ParsedUrl parsed;
    parsed.scheme = base::utils::to_lower(std::string(url.substr(0, scheme_end)));
    parsed.tls = parsed.scheme == "https";
    if (parsed.scheme != "http" && parsed.scheme != "https") {
        throw std::runtime_error("unsupported url scheme: " + parsed.scheme);
    }
    auto authority_begin = scheme_end + 3;
    auto path_begin = url.find('/', authority_begin);
    auto authority = path_begin == std::string_view::npos ? url.substr(authority_begin) : url.substr(authority_begin, path_begin - authority_begin);
    parsed.target = path_begin == std::string_view::npos ? "/" : std::string(url.substr(path_begin));
    std::string_view::size_type colon = std::string_view::npos;
    if (!authority.empty() && authority[0] == '[') {
        auto closing = authority.find(']');
        if (closing != std::string_view::npos) {
            parsed.host = std::string(authority.substr(0, closing + 1));
            if (closing + 1 < authority.size() && authority[closing + 1] == ':') {
                parsed.port = std::string(authority.substr(closing + 2));
            } else {
                parsed.port = parsed.tls ? "443" : "80";
            }
        } else {
            colon = authority.rfind(':');
        }
    } else {
        colon = authority.rfind(':');
    }
    if (colon != std::string_view::npos && parsed.host.empty()) {
        parsed.host = std::string(authority.substr(0, colon));
        parsed.port = std::string(authority.substr(colon + 1));
    } else if (parsed.host.empty()) {
        parsed.host = std::string(authority);
        parsed.port = parsed.tls ? "443" : "80";
    }
    if (parsed.host.empty()) {
        throw std::runtime_error("url missing host");
    }
    return parsed;
}

std::string HttpClient::build_request(std::string_view method,
                                      const ParsedUrl& url,
                                      std::string_view body,
                                      const container::Vector<container::String>& headers,
                                      bool keep_alive) {
    static constexpr std::string_view http_version = " HTTP/1.1\r\nHost: ";
    static constexpr std::string_view fixed_headers = "\r\nUser-Agent: BenGear/0.1\r\nAccept: */*\r\nConnection: ";
    static constexpr std::string_view keep_alive_val = "keep-alive\r\n";
    static constexpr std::string_view close_val = "close\r\n";
    static constexpr std::string_view content_length_hdr = "Content-Length: ";
    static constexpr std::string_view header_end = "\r\n\r\n";

    size_t total_size = 512 + body.size();
    for (const auto& header : headers) {
        total_size += header.size() + 2;
    }
    if (!body.empty()) {
        total_size += content_length_hdr.size() + 20;
    }

    std::string request;
    request.reserve(total_size);

    request.append(method);
    request += ' ';
    request.append(url.target);
    request.append(http_version);
    request.append(url.host);
    request.append(fixed_headers);
    request.append(keep_alive ? keep_alive_val : close_val);

    for (const auto& header : headers) {
        request.append(header.c_str(), header.size());
        request.append("\r\n");
    }

    if (!body.empty()) {
        request.append(content_length_hdr);
        request.append(std::to_string(body.size()));
    }

    request.append(header_end);
    request.append(body);
    return request;
}

container::Map<container::String, std::string> HttpClient::parse_headers(std::string_view header_block) {
    container::Map<container::String, std::string> headers;
    std::size_t begin = 0;
    for (;;) {
        auto end = header_block.find("\r\n", begin);
        auto line = end == std::string_view::npos ? header_block.substr(begin) : header_block.substr(begin, end - begin);
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            auto key_sv = line.substr(0, colon);
            auto value_sv = line.substr(colon + 1);
            std::string key_str(key_sv);
            for (auto& c : key_str) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            auto key_start = key_str.find_first_not_of(" \t");
            auto key_end = key_str.find_last_not_of(" \t");
            if (key_start != std::string::npos) {
                key_str = key_str.substr(key_start, key_end - key_start + 1);
            }
            while (!value_sv.empty() && (value_sv.front() == ' ' || value_sv.front() == '\t')) {
                value_sv.remove_prefix(1);
            }
            while (!value_sv.empty() && (value_sv.back() == ' ' || value_sv.back() == '\t')) {
                value_sv.remove_suffix(1);
            }
            headers[container::String(key_str.c_str())] = std::string(value_sv);
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 2;
    }
    return headers;
}

bool HttpClient::parse_chunk_size(std::string_view line, std::size_t& size) noexcept {
    if (auto extension = line.find(';'); extension != std::string_view::npos) {
        line = line.substr(0, extension);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
        line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return false;
    }

    std::size_t value = 0;
    for (const char ch : line) {
        unsigned digit = 0;
        if (ch >= '0' && ch <= '9') {
            digit = static_cast<unsigned>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            digit = static_cast<unsigned>(ch - 'a' + 10);
        } else if (ch >= 'A' && ch <= 'F') {
            digit = static_cast<unsigned>(ch - 'A' + 10);
        } else {
            return false;
        }
        if (value > (static_cast<std::size_t>(-1) - digit) / 16) {
            return false;
        }
        value = value * 16 + digit;
    }
    size = value;
    return true;
}

} // namespace ben_gear::net
