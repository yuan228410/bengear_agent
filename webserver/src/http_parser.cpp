#include "webserver/http_parser.hpp"
#include "webserver/logging.hpp"

#include <cstring>
#include <sstream>

namespace ws {

// ============ HttpParser ============

HttpParser::HttpParser()
    : state_(State::METHOD)
    , content_length_(0)
    , chunked_(false)
    , current_chunk_size_(0)
    , parsing_body_(false)
{}

void HttpParser::reset() {
    state_ = State::METHOD;
    content_length_ = 0;
    chunked_ = false;
    current_chunk_size_ = 0;
    parsing_body_ = false;
    request_ = HttpRequest{};
    buffer_.clear();
}

HttpParser::Result HttpParser::parse(const char* data, size_t len) {
    buffer_.append(data, len);
    return parse_buffer();
}

HttpParser::Result HttpParser::parse_buffer() {
    while (!buffer_.empty()) {
        switch (state_) {
            case State::METHOD:
                if (!parse_method_line()) return Result::INCOMPLETE;
                break;

            case State::HEADERS:
                if (!parse_headers()) return Result::INCOMPLETE;
                break;

            case State::BODY:
                if (!parse_body()) return Result::INCOMPLETE;
                break;

            case State::DONE:
                return Result::COMPLETE;

            case State::ERROR:
                return Result::ERROR;
        }
    }

    return state_ == State::DONE ? Result::COMPLETE : Result::INCOMPLETE;
}

bool HttpParser::parse_method_line() {
    // 查找行尾
    auto line_end = buffer_.find("\r\n");
    if (line_end == container::String::npos) {
        return false;  // 不完整
    }

    container::String line(buffer_.data(), line_end);

    // 解析请求行: METHOD PATH HTTP/1.1
    size_t pos = 0;
    auto next_token = [&](container::String& out) -> bool {
        auto end = line.find(' ', pos);
        if (end == container::String::npos) return false;
        out = line.substr(pos, end - pos);
        pos = end + 1;
        return true;
    };

    container::String method_str, path_str, version_str;
    if (!next_token(method_str) || !next_token(path_str) || !next_token(version_str)) {
        log::error_fmt("HttpParser: invalid request line");
        state_ = State::ERROR;
        return false;
    }

    request_.method = HttpRouter::string_to_method(method_str.c_str());
    request_.path = path_str.c_str();
    request_.version = version_str.c_str();

    log::debug_fmt("HttpParser: {} {}", method_str.c_str(), path_str.c_str());

    // 消费请求行
    buffer_.erase(0, line_end + 2);
    state_ = State::HEADERS;
    return true;
}

bool HttpParser::parse_headers() {
    while (true) {
        auto line_end = buffer_.find("\r\n");
        if (line_end == container::String::npos) {
            return false;  // 不完整
        }

        container::String line(buffer_.data(), line_end);

        if (line.empty()) {
            // 空行 = 头部结束
            buffer_.erase(0, 2);  // 消费 \r\n

            // 检查 Content-Length 和 Transfer-Encoding
            auto cl_it = request_.headers.find("Content-Length");
            if (cl_it != request_.headers.end()) {
                content_length_ = std::stoul(cl_it->second.c_str());
            }

            auto te_it = request_.headers.find("Transfer-Encoding");
            if (te_it != request_.headers.end() && te_it->second == "chunked") {
                chunked_ = true;
            }

            if (content_length_ > 0 || chunked_) {
                state_ = State::BODY;
            } else {
                state_ = State::DONE;
            }
            return true;
        }

        // 解析头部: Name: Value
        auto colon_pos = line.find(':');
        if (colon_pos != container::String::npos) {
            container::String name = line.substr(0, colon_pos);
            container::String value = line.substr(colon_pos + 2);  // 跳过 ": "
            if (value.size() >= 2 && value.front() == ' ') {
                value = value.substr(1);
            }
            request_.headers[name.c_str()] = value.c_str();
        }

        buffer_.erase(0, line_end + 2);
    }
}

bool HttpParser::parse_body() {
    if (chunked_) {
        return parse_chunked_body();
    }

    if (content_length_ > 0) {
        if (buffer_.size() < content_length_) {
            return false;  // 不完整
        }

        request_.body = container::String(buffer_.data(), content_length_);
        buffer_.erase(0, content_length_);
        state_ = State::DONE;
        return true;
    }

    state_ = State::DONE;
    return true;
}

bool HttpParser::parse_chunked_body() {
    while (true) {
        if (current_chunk_size_ == 0) {
            // 解析 chunk size 行
            auto line_end = buffer_.find("\r\n");
            if (line_end == container::String::npos) {
                return false;
            }

            container::String size_line(buffer_.data(), line_end);
            current_chunk_size_ = std::stoul(size_line.c_str(), nullptr, 16);

            buffer_.erase(0, line_end + 2);

            if (current_chunk_size_ == 0) {
                // 最后一个 chunk，消费尾部 \r\n
                if (buffer_.size() < 2) return false;
                buffer_.erase(0, 2);
                state_ = State::DONE;
                return true;
            }
        }

        // 读取 chunk data
        if (buffer_.size() < current_chunk_size_ + 2) {
            return false;  // 不完整（+2 for \r\n）
        }

        request_.body.append(container::String(buffer_.data(), current_chunk_size_));
        buffer_.erase(0, current_chunk_size_ + 2);  // 跳过 data + \r\n
        current_chunk_size_ = 0;
    }
}

const HttpRequest& HttpParser::request() const {
    return request_;
}

}  // namespace ws
