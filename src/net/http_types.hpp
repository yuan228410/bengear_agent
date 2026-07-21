#pragma once

#include <string>
#include <unordered_map>

namespace ben_gear::net {

/// HTTP 响应 — 纯数据结构，无重型依赖
struct HttpResponse {
    int status = 0;      ///< HTTP 状态码
    std::string body;    ///< 响应体
    std::unordered_map<std::string, std::string> headers;  ///< 响应头（键为小写）
    bool callback_stopped = false;  ///< 回调主动停止（流式请求中解析器提前结束）

    /// 检查响应是否成功
    /// @return true 表示 HTTP 状态码为 2xx
    bool ok() const { return status >= 200 && status < 300; }
};

}  // namespace ben_gear::net
