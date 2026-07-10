#include "schannel_engine.hpp"

#include "ben_gear/base/log/logger.hpp"

#ifdef _WIN32

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <schannel.h>
#include <sspi.h>
#include <security.h>
#include <cryptuiapi.h>
#include <wincrypt.h>
#include <cstdio>

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ncrypt.lib")

// SDK 10.0.26100.0 未定义 SCH_CRED_SNI_ENABLE，但运行时支持
#ifndef SCH_CRED_SNI_ENABLE
#define SCH_CRED_SNI_ENABLE  0x00000040
#endif
// 对于 Win10 22H2 可能不支持 SCHANNEL_CRED_VERSION_400
// 使用版本 2 以确保兼容性
#ifndef SCHANNEL_CRED_VERSION_200
#define SCHANNEL_CRED_VERSION_200  2
#endif

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
    // 核心模式：
    //   1. 用当前累积的数据（in_buf/in_buf_size）调用 InitSecCtx
    //   2. 处理结果：发送输出 / 读更多数据 / 完成
    //   3. 循环直到 SEC_E_OK
    char in_buf[16384];
    char out_buf[16384];
    DWORD in_buf_size = 0;
    bool first_call = true;

    // SNI 主机名（在整个循环中有效）
    LPWSTR target_name = nullptr;
    std::wstring whostname;
    if (!host.empty()) {
        int len = MultiByteToWideChar(CP_UTF8, 0, host.data(),
                                      static_cast<int>(host.size()), nullptr, 0);
        whostname.resize(len);
        MultiByteToWideChar(CP_UTF8, 0, host.data(),
                            static_cast<int>(host.size()), &whostname[0], len);
        target_name = const_cast<LPWSTR>(whostname.c_str());
    }

    for (;;) {
        // 输入 buffer
        SecBufferDesc in_desc = {};
        SecBuffer in_buffers[2] = {};

        if (in_buf_size > 0) {
            in_desc.ulVersion = SECBUFFER_VERSION;
            in_desc.cBuffers = 2;
            in_desc.pBuffers = in_buffers;
            in_buffers[0].cbBuffer = in_buf_size;
            in_buffers[0].BufferType = SECBUFFER_TOKEN;
            in_buffers[0].pvBuffer = in_buf;
            in_buffers[1].cbBuffer = 0;
            in_buffers[1].BufferType = SECBUFFER_EMPTY;
            in_buffers[1].pvBuffer = nullptr;
        }

        // 输出 buffer
        SecBufferDesc out_desc = {};
        SecBuffer out_sec = {};
        out_desc.ulVersion = SECBUFFER_VERSION;
        out_desc.cBuffers = 1;
        out_desc.pBuffers = &out_sec;
        out_sec.cbBuffer = sizeof(out_buf);
        out_sec.BufferType = SECBUFFER_TOKEN;
        out_sec.pvBuffer = out_buf;

        unsigned long attr = 0;
        status = InitializeSecurityContextW(
            &impl_->credential,
            first_call ? nullptr : &impl_->context,
            target_name,
            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
            ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM,
            0,
            SECURITY_NATIVE_DREP,
            (first_call || in_buf_size == 0) ? nullptr : &in_desc,
            0,
            &impl_->context,
            &out_desc,
            &attr,
            nullptr);



        // 处理结果
        if (status == SEC_E_OK) {
            // 握手完成，发送最后的输出
            if (out_sec.cbBuffer > 0) {
                co_await loop.wait_write(fd);
                auto sent = ::send(fd, static_cast<const char*>(out_sec.pvBuffer),
                                   out_sec.cbBuffer, 0);
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

        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            // 当前 TLS 记录不完整：读更多数据追加到 in_buf
            co_await loop.wait_read(fd);
            auto recv_len = ::recv(fd, in_buf + in_buf_size,
                                   static_cast<int>(sizeof(in_buf) - in_buf_size), 0);
            if (recv_len <= 0) {
                throw std::runtime_error("SchannelEngine: connection closed during handshake");
            }
            in_buf_size += static_cast<DWORD>(recv_len);
            // 非阻塞读取更多可用数据
            for (int drain = 0; drain < 5; drain++) {
                u_long available = 0;
                if (ioctlsocket(fd, FIONREAD, &available) != 0 || available == 0) break;
                auto more = ::recv(fd, in_buf + in_buf_size,
                                   static_cast<int>(sizeof(in_buf) - in_buf_size), 0);
                if (more <= 0) break;
                in_buf_size += static_cast<DWORD>(more);
            }
            first_call = false;
            continue;  // 用追加后的数据再次调用
        }

        if (status == SEC_I_CONTINUE_NEEDED) {
            if (first_call) {
                impl_->has_context = true;
            }

            // 处理 SECBUFFER_EXTRA：未被消耗的输入数据
            if (!first_call && in_buf_size > 0) {
                DWORD consumed = 0;
                for (int i = 0; i < 2; i++) {
                    if (in_buffers[i].BufferType == SECBUFFER_EXTRA && in_buffers[i].cbBuffer > 0) {
                        consumed = in_buf_size - in_buffers[i].cbBuffer;
                        memmove(in_buf, in_buf + consumed, in_buffers[i].cbBuffer);
                        in_buf_size = in_buffers[i].cbBuffer;
                        break;
                    }
                }
                if (consumed == 0) {
                    in_buf_size = 0;  // 无 extra，全部消耗
                }
            } else {
                in_buf_size = 0;
            }

            // 发送输出（只对合法状态发送）
            bool output_sent = false;
            if (out_sec.cbBuffer > 0 && out_sec.cbBuffer < sizeof(out_buf)) {
                co_await loop.wait_write(fd);
                auto sent = ::send(fd, static_cast<const char*>(out_sec.pvBuffer),
                                   out_sec.cbBuffer, 0);
                if (sent <= 0) {
                    throw std::runtime_error("SchannelEngine: failed to send handshake data");
                }
                output_sent = true;
            }

            // 非首次调用的输出（客户端 Finished）发送后，用空输入检查握手是否完成
            if (output_sent && !first_call && in_buf_size == 0) {
                // 回到循环顶部用空输入（pInput=nullptr）调用 InitSecCtx
                // 如果握手已完成则返回 SEC_E_OK，否则需要更多服务器数据
                first_call = false;
                continue;
            }

            // 读输入
            if (in_buf_size == 0) {
                co_await loop.wait_read(fd);
                auto recv_len = ::recv(fd, in_buf, sizeof(in_buf), 0);
                if (recv_len <= 0) {
                    throw std::runtime_error("SchannelEngine: connection closed during handshake");
                }
                in_buf_size = static_cast<DWORD>(recv_len);
            }

            first_call = false;
            continue;
        }

        // 其他错误：可能握手已完成，剩余数据是加密的后握手数据
        DWORD saved_in_buf_size = in_buf_size;
        char hexbuf[32];
        snprintf(hexbuf, sizeof(hexbuf), "0x%08lX (%ld)", (unsigned long)status, (long)status);
        log::error_fmt("SchannelEngine: InitializeSecurityContextW failed: {} (first_call={})",
                       hexbuf, first_call);
        // 检查握手是否实际上已完成
        if (!first_call && impl_->has_context) {
            SecPkgContext_ConnectionInfo conn_info = {};
            SECURITY_STATUS qs = QueryContextAttributesW(
                &impl_->context, SECPKG_ATTR_CONNECTION_INFO, &conn_info);
            if (SUCCEEDED(qs) && conn_info.dwProtocol != 0 &&
                conn_info.dwProtocol != SP_PROT_NONE) {
                // 握手实际已完成，保存剩余的加密数据等待 DecryptMessage 处理
                impl_->connected = true;
                QueryContextAttributesW(&impl_->context, SECPKG_ATTR_STREAM_SIZES, &impl_->sizes);
                log::info_fmt("SchannelEngine: handshake completed (post-handshake data, error={})",
                              hexbuf);
                co_return;
            }
        }
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
        &chain_para, 0,
        nullptr, &chain_ctx);

    if (!ok || !chain_ctx) {
        CertFreeCertificateContext(remote_cert);
        throw std::runtime_error("SchannelEngine: failed to build certificate chain");
    }

    // 验证策略
    // ★ 修复：创建持久 wstring，避免临时对象导致悬垂指针
    std::wstring wserver_name;
    if (!impl_->hostname.empty()) {
        wserver_name.assign(impl_->hostname.begin(), impl_->hostname.end());
    }

    HTTPSPolicyCallbackData policy = {};
    policy.cbStruct = sizeof(HTTPSPolicyCallbackData);
    policy.dwAuthType = AUTHTYPE_SERVER;
    policy.fdwChecks = 0;
    policy.pwszServerName = wserver_name.empty() ? nullptr :
        const_cast<LPWSTR>(wserver_name.c_str());

    CERT_CHAIN_POLICY_PARA policy_para = {};
    policy_para.cbSize = sizeof(policy_para);
    policy_para.pvExtraPolicyPara = &policy;

    CERT_CHAIN_POLICY_STATUS policy_status = {};
    policy_status.cbSize = sizeof(policy_status);

    ok = CertVerifyCertificateChainPolicy(
        CERT_CHAIN_POLICY_SSL, chain_ctx, &policy_para, &policy_status);

    DWORD verify_error = policy_status.dwError;
    CertFreeCertificateChain(chain_ctx);
    CertFreeCertificateContext(remote_cert);

    if (!ok || verify_error != 0) {
        log::error_fmt("SchannelEngine: certificate verification failed: ok={} error={}",
                       ok ? 1 : 0, verify_error);
        throw std::runtime_error("SchannelEngine: certificate verification failed: " +
                                  std::to_string(verify_error));
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
        // 如果 recv_buffer 没有剩余数据（EXTRA），才等网络
        if (impl_->recv_buffer.empty()) {
            co_await loop.wait_read(impl_->fd);
            char tmp[16384];
            auto recv_len = ::recv(impl_->fd, tmp, sizeof(tmp), 0);
            if (recv_len <= 0) {
                co_return 0;
            }
            impl_->recv_buffer.insert(impl_->recv_buffer.end(), tmp, tmp + recv_len);
        }

        // 尝试解密
        bool progress = false;
        while (!impl_->recv_buffer.empty()) {

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
                progress = true;
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

                // 先复制 EXTRA 再 clear（避免 DECRYPT 修改 recv_buffer 后指针引用失效）
                std::vector<char> extra_buf;
                for (int i = 0; i < 4; ++i) {
                    if (buffers[i].BufferType == SECBUFFER_EXTRA && buffers[i].cbBuffer > 0) {
                        auto* extra = static_cast<char*>(buffers[i].pvBuffer);
                        extra_buf.assign(extra, extra + buffers[i].cbBuffer);
                        break;
                    }
                }
                impl_->recv_buffer = std::move(extra_buf);

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
                continue;  // 解密成功但无数据（空记录），继续处理下一条
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

        // 内层循环没进展（SEC_E_INCOMPLETE_MESSAGE），但 recv_buffer 还有数据
        // → 等更多网络数据到达后再试
        if (!progress) {
            co_await loop.wait_read(impl_->fd);
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
