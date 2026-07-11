#pragma once

#include <string>

namespace ben_gear::net {

/// TLS 配置（后端无关）
struct TlsConfig {
    bool verify_peer = true;          // 是否验证服务端证书
    bool enable_sni = true;           // 是否发送 SNI
    std::string ca_cert_path;         // 自定义 CA 路径（空=系统默认）
    std::string client_cert_path;     // 客户端证书路径（双向 TLS）
    std::string client_key_path;      // 客户端私钥路径
    int min_protocol_version = 0;     // 0=默认(TLS 1.2+), 12=TLS 1.2, 13=TLS 1.3
};

}  // namespace ben_gear::net
