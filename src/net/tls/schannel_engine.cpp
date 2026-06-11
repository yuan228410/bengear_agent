#include "schannel_engine.hpp"

#include "ben_gear/base/log/logger.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <schannel.h>
#include <sspi.h>
#include <security.h>
#include <cryptuiapi.h>
#include <wincrypt.h>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ncrypt.lib")

namespace ben_gear::net {

// ==================== Schannel Engine 常量 ====================

// Schannel 加密记录最大大小
static constexpr size_t kSchannelMaxRecordSize = 16384;
// 额外开销（加密头 + 尾部 + 填充）
static constexpr size_t kSchannelHeaderSize = 64;
static constexpr size_t kSchannelTrailerSize = 64;

// ==================== SchannelEngine::Session::Impl ====================

struct SchannelEngine::Session::Impl {
    CtxtHandle context;             // 安全上下文句柄
    CredHandle credential;          // 凭据句柄
    bool has_context = false;
    bool has_credential = false;
    bool connected = false;

    socket_handle fd = invalid_socket_handle;

    // 加密参数
    SecPkgContext_StreamSizes sizes;  // 加密/解密缓冲区大小

    // 解密 leftover 缓冲区
    std::vector<char> recv_buffer;
    std::vector<char> decrypted_data;
    std::size_t decrypted_offset = 0;

    // SNI 主机名
    std::string hostname;

    // 证书验证
    bool verify_peer = true;
};

// ==================== SchannelEngine::Session ====================

SchannelEngine::Session::Session()
    : impl_(new Impl()) {
    SecInvalidateHandle(&impl_->context);
    SecInvalidateHandle(&impl_->credential);
}

SchannelEngine::Session::~Session() {
    if (impl_) {
        if (impl_->has_context) {
            DeleteSecurityContext(&impl_->context);
        }
        if (impl_->has_credential) {
            FreeCredentialsHandle(&impl_->credential);
        }
        delete impl_;
        impl_ = nullptr;
    }
}

Task<void> SchannelEngine::Session::handshake(EventLoop& loop, socket_handle fd,
                                               std::string_view host,
                                               const TlsConfig& config) {
    impl_->fd = fd;
    impl_->hostname = std::string(host);
    impl_->verify_peer = config.verify_peer;

    // 1. 获取凭据
    SCHANNEL_CRED cred_data = {};
    cred_data.dwVersion = SCHANNEL_CRED_VERSION;
    cred_data.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

    // 协议版本
    if (config.min_protocol_version >= 13) {
        cred_data.grbitEnabledProtocols = SP_PROT_TLS1_3_SERVER | SP_PROT_TLS1_3_CLIENT;
    } else if (config.min_protocol_version >= 12) {
        cred_data.grbitEnabledProtocols = SP_PROT_TLS1_2_SERVER | SP_PROT_TLS1_2_CLIENT |
                                           SP_PROT_TLS1_3_SERVER | SP_PROT_TLS1_3_CLIENT;
    }
    // 0 = 使用系统默认（Win10+ 通常 TLS 1.2+）

    SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr,
        const_cast<LPWSTR>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND,
        nullptr,
        &cred_data,
        nullptr,
        nullptr,
        &impl_->credential,
        nullptr);

    if (status != SEC_E_OK) {
        throw std::runtime_error("SchannelEngine: AcquireCredentialsHandle failed: " +
                                  std::to_string(status));
    }
    impl_->has_credential = true;

    // 2. 握手循环
    std::vector<char> in_buffer(8192);
    std::vector<char> out_buffer(8192);
    DWORD in_buffer_size = 0;
    bool first_call = true;

    for (;;) {
        SecBufferDesc in_desc = {};
        SecBuffer in_buffers[2] = {};

        if (!first_call && in_buffer_size > 0) {
            in_desc.ulVersion = SECBUFFER_VERSION;
            in_desc.cBuffers = 2;
            in_desc.pBuffers = in_buffers;

            in_buffers[0].cbBuffer = in_buffer_size;
            in_buffers[0].BufferType = SECBUFFER_TOKEN;
            in_buffers[0].pvBuffer = in_buffer.data();

            in_buffers[1].cbBuffer = 0;
            in_buffers[1].BufferType = SECBUFFER_EMPTY;
            in_buffers[1].pvBuffer = nullptr;
        }

        SecBufferDesc out_desc = {};
        SecBuffer out_buf = {};
        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers = 1;
        out_desc.pBuffers = &out_buf;
        out_buf.cbBuffer = static_cast<DWORD>(out_buffer.size());
        out_buf.BufferType = SECBUFFER_TOKEN;
        out_buf.pvBuffer = out_buffer.data();

        // SNI 主机名
        LPWSTR target_name = nullptr;
        std::wstring whostname;
        if (config.enable_sni && !host.empty()) {
            int len = MultiByteToWideChar(CP_UTF8, 0, host.data(),
                                          static_cast<int>(host.size()), nullptr, 0);
            whostname.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, host.data(),
                                static_cast<int>(host.size()), &whostname[0], len);
            target_name = whostname.data();
        }

        unsigned long attr = 0;
        status = InitializeSecurityContextW(
            &impl_->credential,
            first_call ? nullptr : &impl_->context,
            target_name,
            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY |
            ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION,
            0,
            SECURITY_NATIVE_DREP,
            first_call ? nullptr : &in_desc,
            0,
            first_call ? &impl_->context : nullptr,
            &out_desc,
            &attr,
            nullptr);

        if (status == SEC_E_OK) {
            // 握手完成，发送最后的输出
            if (out_buf.cbBuffer > 0) {
                co_await loop.wait_write(fd);
                auto sent = ::send(fd, static_cast<const char*>(out_buf.pvBuffer),
                                   out_buf.cbBuffer, 0);
                FreeContextBuffer(out_buf.pvBuffer);
                if (sent <= 0) {
                    throw std::runtime_error("SchannelEngine: failed to send final handshake data");
                }
            }

            // 证书验证
            if (impl_->verify_peer) {
                verify_certificate();
            }

            // 获取加密参数
            QueryContextAttributesW(&impl_->context, SECPKG_ATTR_STREAM_SIZES, &impl_->sizes);

            impl_->has_context = true;
            impl_->connected = true;
            log::debug_fmt("SchannelEngine: handshake completed for {}", host);
            co_return;
        }

        if (status == SEC_I_CONTINUE_NEEDED) {
            if (first_call) {
                impl_->has_context = true;  // context 已创建
            }

            // 发送输出数据
            if (out_buf.cbBuffer > 0) {
                co_await loop.wait_write(fd);
                auto sent = ::send(fd, static_cast<const char*>(out_buf.pvBuffer),
                                   out_buf.cbBuffer, 0);
                FreeContextBuffer(out_buf.pvBuffer);
                if (sent <= 0) {
                    throw std::runtime_error("SchannelEngine: failed to send handshake data");
                }
            }

            // 读取服务端响应
            in_buffer_size = 0;
            for (;;) {
                co_await loop.wait_read(fd);
                auto recv_len = ::recv(fd, in_buffer.data() + in_buffer_size,
                                       static_cast<int>(in_buffer.size() - in_buffer_size), 0);
                if (recv_len <= 0) {
                    throw std::runtime_error("SchannelEngine: connection closed during handshake");
                }
                in_buffer_size += static_cast<DWORD>(recv_len);

                // 尝试用当前数据继续握手
                break;
            }

            first_call = false;
            continue;
        }

        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // 需要更多数据
            co_await loop.wait_read(fd);
            auto recv_len = ::recv(fd, in_buffer.data() + in_buffer_size,
                                   static_cast<int>(in_buffer.size() - in_buffer_size), 0);
            if (recv_len <= 0) {
                throw std::runtime_error("SchannelEngine: connection closed during handshake");
            }
            in_buffer_size += static_cast<DWORD>(recv_len);
            first_call = false;
            continue;
        }

        // 其他错误
        throw std::runtime_error("SchannelEngine: handshake failed: " +
                                  std::to_string(status));
    }
}

void SchannelEngine::Session::verify_certificate() {
    PCCERT_CONTEXT remote_cert = nullptr;
    SECURITY_STATUS status = QueryContextAttributesW(
        &impl_->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &remote_cert);

    if (status != SEC_E_OK || !remote_cert) {
        throw std::runtime_error("SchannelEngine: failed to get remote certificate");
    }

    // 使用 Windows 证书链验证
    CERT_CHAIN_PARA chain_para = {};
    chain_para.cbSize = sizeof(chain_para);

    PCCERT_CHAIN_CONTEXT chain_ctx = nullptr;
    BOOL ok = CertGetCertificateChain(
        nullptr, remote_cert, nullptr, nullptr,
        &chain_para, CERT_CHAIN_REVOCATION_CHECK_NONE,
        nullptr, &chain_ctx);

    if (!ok || !chain_ctx) {
        CertFreeCertificateContext(remote_cert);
        throw std::runtime_error("SchannelEngine: failed to build certificate chain");
    }

    // 验证策略
    HTTPSPolicyCallbackData policy = {};
    policy.cbStruct = sizeof(HTTPSPolicyCallbackData);
    policy.dwAuthType = AUTHTYPE_SERVER;
    policy.fdwChecks = 0;
    policy.pwszServerName = impl_->hostname.empty() ? nullptr :
        const_cast<LPWSTR>(std::wstring(impl_->hostname.begin(), impl_->hostname.end()).c_str());

    CERT_CHAIN_POLICY_PARA policy_para = {};
    policy_para.cbSize = sizeof(policy_para);
    policy_para.pvExtraPolicyPara = &policy;

    CERT_CHAIN_POLICY_STATUS policy_status = {};
    policy_status.cbSize = sizeof(policy_status);

    ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_SSL, chain_ctx, &policy_para, &policy_status);

    CertFreeCertificateChain(chain_ctx);
    CertFreeCertificateContext(remote_cert);

    if (!ok || policy_status.dwError != 0) {
        throw std::runtime_error("SchannelEngine: certificate verification failed");
    }
}

Task<void> SchannelEngine::Session::write_all(EventLoop& loop, std::string_view data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        auto chunk_size = std::min(data.size() - offset,
                                   static_cast<std::size_t>(impl_->sizes.cbMaximumMessage));
        auto* chunk_data = reinterpret_cast<const unsigned char*>(data.data() + offset);
        auto chunk_len = static_cast<DWORD>(chunk_size);

        // 计算加密后缓冲区大小
        DWORD total_size = impl_->sizes.cbHeader + chunk_len + impl_->sizes.cbTrailer;
        std::vector<char> encrypted(total_size);

        // 填充 header
        std::memcpy(encrypted.data(), chunk_data, 0);  // 占位，EncryptMessage 会填充

        SecBuffer buffers[4] = {};
        SecBufferDesc desc = {};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        // header
        buffers[0].cbBuffer = impl_->sizes.cbHeader;
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer = encrypted.data();

        // 明文数据
        buffers[1].cbBuffer = chunk_len;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer = encrypted.data() + impl_->sizes.cbHeader;
        std::memcpy(buffers[1].pvBuffer, chunk_data, chunk_len);

        // trailer
        buffers[2].cbBuffer = impl_->sizes.cbTrailer;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = encrypted.data() + impl_->sizes.cbHeader + chunk_len;

        // empty
        buffers[3].cbBuffer = 0;
        buffers[3].BufferType = SECBUFFER_EMPTY;

        SECURITY_STATUS status = EncryptMessage(&impl_->context, 0, &desc, 0);
        if (status != SEC_E_OK) {
            throw std::runtime_error("SchannelEngine: EncryptMessage failed");
        }

        // 发送完整加密记录
        std::size_t total_sent = 0;
        for (int i = 0; i < 3; ++i) {
            total_sent += buffers[i].cbBuffer;
        }

        // 重组加密数据
        std::vector<char> send_buf(total_sent);
        std::size_t pos = 0;
        for (int i = 0; i < 3; ++i) {
            if (buffers[i].cbBuffer > 0) {
                std::memcpy(send_buf.data() + pos, buffers[i].pvBuffer, buffers[i].cbBuffer);
                pos += buffers[i].cbBuffer;
            }
        }

        // 发送
        std::size_t written = 0;
        while (written < send_buf.size()) {
            co_await loop.wait_write(impl_->fd);
            auto result = ::send(impl_->fd, send_buf.data() + written,
                                 static_cast<int>(send_buf.size() - written), 0);
            if (result <= 0) {
                throw std::runtime_error("SchannelEngine: send failed");
            }
            written += static_cast<std::size_t>(result);
        }

        offset += chunk_size;
    }
}

Task<std::size_t> SchannelEngine::Session::read_some(EventLoop& loop,
                                                      char* buf, std::size_t size) {
    // 先返回 leftover 解密数据
    if (impl_->decrypted_offset < impl_->decrypted_data.size()) {
        auto avail = std::min(size, impl_->decrypted_data.size() - impl_->decrypted_offset);
        std::memcpy(buf, impl_->decrypted_data.data() + impl_->decrypted_offset, avail);
        impl_->decrypted_offset += avail;
        if (impl_->decrypted_offset >= impl_->decrypted_data.size()) {
            impl_->decrypted_data.clear();
            impl_->decrypted_offset = 0;
        }
        co_return avail;
    }

    // 从网络读取加密数据并解密
    for (;;) {
        // 读取数据
        co_await loop.wait_read(impl_->fd);
        char tmp[16384];
        auto recv_len = ::recv(impl_->fd, tmp, sizeof(tmp), 0);
        if (recv_len <= 0) {
            co_return 0;
        }

        // 追加到接收缓冲区
        impl_->recv_buffer.insert(impl_->recv_buffer.end(), tmp, tmp + recv_len);

        // 尝试解密
        while (!impl_->recv_buffer.empty()) {
            std::vector<char> output(impl_->recv_buffer.size() + 1024);

            SecBuffer buffers[4] = {};
            SecBufferDesc desc = {};
            desc.ulVersion = SECBUFFER_VERSION;
            desc.cBuffers = 4;
            desc.pBuffers = buffers;

            buffers[0].cbBuffer = static_cast<DWORD>(impl_->recv_buffer.size());
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[0].pvBuffer = impl_->recv_buffer.data();

            buffers[1].cbBuffer = 0;
            buffers[1].BufferType = SECBUFFER_EMPTY;
            buffers[2].cbBuffer = 0;
            buffers[2].BufferType = SECBUFFER_EMPTY;
            buffers[3].cbBuffer = 0;
            buffers[3].BufferType = SECBUFFER_EMPTY;

            SECURITY_STATUS status = DecryptMessage(&impl_->context, &desc, 0, nullptr);

            if (status == SEC_E_OK) {
                // 提取明文
                impl_->decrypted_data.clear();
                impl_->decrypted_offset = 0;

                for (int i = 0; i < 4; ++i) {
                    if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0) {
                        auto* data = static_cast<char*>(buffers[i].pvBuffer);
                        impl_->decrypted_data.insert(impl_->decrypted_data.end(),
                                                     data, data + buffers[i].cbBuffer);
                    }
                }

                // 处理剩余缓冲区
                impl_->recv_buffer.clear();
                for (int i = 0; i < 4; ++i) {
                    if (buffers[i].BufferType == SECBUFFER_EXTRA && buffers[i].cbBuffer > 0) {
                        auto* extra = static_cast<char*>(buffers[i].pvBuffer);
                        impl_->recv_buffer.insert(impl_->recv_buffer.end(),
                                                  extra, extra + buffers[i].cbBuffer);
                    }
                }

                // 返回数据
                if (!impl_->decrypted_data.empty()) {
                    auto avail = std::min(size, impl_->decrypted_data.size());
                    std::memcpy(buf, impl_->decrypted_data.data(), avail);
                    impl_->decrypted_offset = avail;
                    if (avail >= impl_->decrypted_data.size()) {
                        impl_->decrypted_data.clear();
                        impl_->decrypted_offset = 0;
                    }
                    co_return avail;
                }
                continue;  // 解密成功但无数据，继续处理
            }

            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                // 需要更多数据
                break;
            }

            if (status == SEC_I_CONTEXT_EXPIRED) {
                co_return 0;  // 连接关闭
            }

            throw std::runtime_error("SchannelEngine: DecryptMessage failed: " +
                                      std::to_string(status));
        }
    }
}

void* SchannelEngine::Session::native_handle() noexcept {
    return impl_;
}

void SchannelEngine::Session::shutdown() noexcept {
    if (impl_ && impl_->connected) {
        // 发送 close_notify
        DWORD token = SCHANNEL_SHUTDOWN;
        SecBuffer shut_buf = {};
        shut_buf.cbBuffer = sizeof(token);
        shut_buf.BufferType = SECBUFFER_TOKEN;
        shut_buf.pvBuffer = &token;

        SecBufferDesc desc = {};
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 1;
        desc.pBuffers = &shut_buf;

        ApplyControlToken(&impl_->context, &desc);

        // 发送关闭通知
        SecBuffer out_buf = {};
        SecBufferDesc out_desc = {};
        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers = 1;
        out_desc.pBuffers = &out_buf;
        out_buf.cbBuffer = 0;
        out_buf.BufferType = SECBUFFER_TOKEN;
        out_buf.pvBuffer = nullptr;

        unsigned long attr = 0;
        InitializeSecurityContextW(
            &impl_->credential, &impl_->context, nullptr,
            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM,
            0, 0, nullptr, 0, nullptr, &out_desc, &attr, nullptr);

        if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
            ::send(impl_->fd, static_cast<const char*>(out_buf.pvBuffer),
                   out_buf.cbBuffer, 0);
            FreeContextBuffer(out_buf.pvBuffer);
        }

        impl_->connected = false;
    }
}

bool SchannelEngine::Session::is_connected() const noexcept {
    return impl_ ? impl_->connected : false;
}

// ==================== SchannelEngine ====================

SchannelEngine::SchannelEngine() = default;

SchannelEngine::~SchannelEngine() = default;

std::unique_ptr<TlsEngine::Session> SchannelEngine::create_session() {
    return std::make_unique<Session>();
}

void SchannelEngine::initialize() {
    if (initialized_) return;
    initialized_ = true;
    log::info_fmt("SchannelEngine: initialized");
}

void SchannelEngine::free_native_handle(void* handle) noexcept {
    if (handle) {
        auto* impl = static_cast<Session::Impl*>(handle);
        if (impl->has_context) {
            DeleteSecurityContext(&impl->context);
        }
        if (impl->has_credential) {
            FreeCredentialsHandle(&impl->credential);
        }
        delete impl;
    }
}

}  // namespace ben_gear::net

#else  // !_WIN32

// 非 Windows 平台的空实现（编译期不应被链接）

namespace ben_gear::net {

SchannelEngine::Session::Session() = default;
SchannelEngine::Session::~Session() = default;

Task<void> SchannelEngine::Session::handshake(EventLoop&, socket_handle,
                                               std::string_view, const TlsConfig&) {
    throw std::runtime_error("SchannelEngine: not available on non-Windows");
}

Task<void> SchannelEngine::Session::write_all(EventLoop&, std::string_view) {
    throw std::runtime_error("SchannelEngine: not available on non-Windows");
}

Task<std::size_t> SchannelEngine::Session::read_some(EventLoop&, char*, std::size_t) {
    throw std::runtime_error("SchannelEngine: not available on non-Windows");
}

void* SchannelEngine::Session::native_handle() noexcept { return nullptr; }
void SchannelEngine::Session::shutdown() noexcept {}
bool SchannelEngine::Session::is_connected() const noexcept { return false; }

SchannelEngine::SchannelEngine() = default;
SchannelEngine::~SchannelEngine() = default;

std::unique_ptr<TlsEngine::Session> SchannelEngine::create_session() {
    throw std::runtime_error("SchannelEngine: not available on non-Windows");
}

void SchannelEngine::initialize() {}
void SchannelEngine::free_native_handle(void*) noexcept {}

}  // namespace ben_gear::net

#endif  // _WIN32
