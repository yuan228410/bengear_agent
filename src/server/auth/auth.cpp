#include "server/auth/auth.hpp"

namespace ben_gear::server {

bool authenticate(const HttpRequest& req,
                  const config::ServerSettings& settings,
                  std::string& username) {
    if (settings.api_key.empty()) {
        // 无 API Key 模式：必须传 username，不生成 anonymous
        auto qit = req.query.find(std::string("username"));
        if (qit != req.query.end() && !qit->second.empty()) {
            username = qit->second;
            return true;
        }
        auto hit = req.headers.find("x-username");
        if (hit != req.headers.end() && !hit->second.empty()) {
            username = hit->second;
            return true;
        }
        return false;
    }
    if (auto it = req.headers.find("authorization"); it != req.headers.end()) {
        const auto& auth = it->second;
        if (auth.size() >= 7 && auth.substr(0, 7) == "Bearer ") {
            auto token = auth.substr(7);
            if (token == settings.api_key) {
                if (auto un = req.headers.find("x-username"); un != req.headers.end())
                    username = un->second;
                else
                    username = "authenticated";
                return true;
            }
        }
    }
    return false;
}

} // namespace ben_gear::server
