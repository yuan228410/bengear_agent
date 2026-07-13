// Minimal Schannel handshake test - standalone, no dependencies
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <windows.h>
#include <schannel.h>
#include <sspi.h>
#include <security.h>
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "secur32.lib")

const char* status_to_str(SECURITY_STATUS s) {
    switch (s) {
    case SEC_E_OK: return "SEC_E_OK";
    case SEC_I_CONTINUE_NEEDED: return "SEC_I_CONTINUE_NEEDED";
    case SEC_I_COMPLETE_NEEDED: return "SEC_I_COMPLETE_NEEDED";
    case SEC_I_COMPLETE_AND_CONTINUE: return "SEC_I_COMPLETE_AND_CONTINUE";
    case SEC_E_INVALID_HANDLE: return "SEC_E_INVALID_HANDLE";
    case SEC_E_INVALID_TOKEN: return "SEC_E_INVALID_TOKEN";
    case SEC_E_CANNOT_PACK: return "SEC_E_CANNOT_PACK";
    case SEC_E_UNSUPPORTED_FUNCTION: return "SEC_E_UNSUPPORTED_FUNCTION";
    case SEC_E_TARGET_UNKNOWN: return "SEC_E_TARGET_UNKNOWN";
    case SEC_E_INTERNAL_ERROR: return "SEC_E_INTERNAL_ERROR";
    case SEC_E_SECPKG_NOT_FOUND: return "SEC_E_SECPKG_NOT_FOUND";
    case SEC_E_BUFFER_TOO_SMALL: return "SEC_E_BUFFER_TOO_SMALL";
    default: return "UNKNOWN";
    }
}

int main() {
    printf("Schannel Test v1\n");
    printf("sizeof(SCHANNEL_CRED) = %zu\n", sizeof(SCHANNEL_CRED));
    printf("SCHANNEL_CRED_VERSION = 0x%08lX\n", (unsigned long)SCHANNEL_CRED_VERSION);
    printf("SECURITY_NATIVE_DREP = 0x%08lX\n", (unsigned long)SECURITY_NATIVE_DREP);

    // Test 1: AcquireCredentialsHandle only
    printf("\n=== Test 1: AcquireCredentialsHandleW ===\n");
    {
        CredHandle cred;
        SecInvalidateHandle(&cred);
        TimeStamp expiry;

        SCHANNEL_CRED cred_data = {};
        cred_data.dwVersion = SCHANNEL_CRED_VERSION;
        cred_data.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

        SECURITY_STATUS status = AcquireCredentialsHandleW(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME_W),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred_data,
            nullptr,
            nullptr,
            &cred,
            &expiry);

        printf("AcquireCredentialsHandleW: %s (0x%08lX)\n", status_to_str(status), (unsigned long)status);

        if (status == SEC_E_OK) {
            printf("  cred.dwLower=0x%p, cred.dwUpper=0x%p\n",
                   (void*)cred.dwLower, (void*)cred.dwUpper);

            // Test 2: InitializeSecurityContextW (first call, no input)
            printf("\n=== Test 2: InitializeSecurityContextW (first call) ===\n");
            {
                CtxtHandle ctx;
                SecInvalidateHandle(&ctx);
                unsigned long attr = 0;

                char out_buf[8192];
                SecBuffer out_sec;
                out_sec.cbBuffer = sizeof(out_buf);
                out_sec.BufferType = SECBUFFER_TOKEN;
                out_sec.pvBuffer = out_buf;

                SecBufferDesc out_desc;
                out_desc.ulVersion = SECBUFFER_VERSION;
                out_desc.cBuffers = 1;
                out_desc.pBuffers = &out_sec;

                // Try with SECURITY_NATIVE_DREP
                printf("  Calling with SECURITY_NATIVE_DREP (0x%08lX)...\n",
                       (unsigned long)SECURITY_NATIVE_DREP);

                status = InitializeSecurityContextW(
                    &cred,
                    nullptr,       // phContext - NULL on first call
                    nullptr,       // pszTargetName - NULL for no SNI
                    ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                    ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM,
                    0,             // Reserved1
                    SECURITY_NATIVE_DREP,
                    nullptr,       // pInput - NULL on first call
                    0,             // Reserved2
                    &ctx,          // phNewContext
                    &out_desc,
                    &attr,
                    nullptr);      // ptsExpiry

                printf("InitializeSecurityContextW: %s (0x%08lX)\n",
                       status_to_str(status), (unsigned long)status);
                printf("  out_sec.cbBuffer = %lu\n", out_sec.cbBuffer);

                if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED) {
                    printf("  SUCCESS! Handshake can proceed.\n");
                }

                DeleteSecurityContext(&ctx);
            }
        }

        FreeCredentialsHandle(&cred);
    }

    // Test 3: Same but with SECURITY_NETWORK_DREP
    printf("\n=== Test 3: InitializeSecurityContextW (SECURITY_NETWORK_DREP) ===\n");
    {
        CredHandle cred;
        SecInvalidateHandle(&cred);
        TimeStamp expiry;

        SCHANNEL_CRED cred_data = {};
        cred_data.dwVersion = SCHANNEL_CRED_VERSION;
        cred_data.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

        SECURITY_STATUS status = AcquireCredentialsHandleW(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME_W),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred_data,
            nullptr,
            nullptr,
            &cred,
            &expiry);

        if (status == SEC_E_OK) {
            CtxtHandle ctx;
            SecInvalidateHandle(&ctx);
            unsigned long attr = 0;

            char out_buf[8192];
            SecBuffer out_sec;
            out_sec.cbBuffer = sizeof(out_buf);
            out_sec.BufferType = SECBUFFER_TOKEN;
            out_sec.pvBuffer = out_buf;

            SecBufferDesc out_desc;
            out_desc.ulVersion = SECBUFFER_VERSION;
            out_desc.cBuffers = 1;
            out_desc.pBuffers = &out_sec;

            printf("  Calling with SECURITY_NETWORK_DREP (0x00000000)...\n");

            status = InitializeSecurityContextW(
                &cred,
                nullptr,
                nullptr,
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM,
                0,
                SECURITY_NETWORK_DREP,  // 0
                nullptr,
                0,
                &ctx,
                &out_desc,
                &attr,
                nullptr);

            printf("InitializeSecurityContextW: %s (0x%08lX)\n",
                   status_to_str(status), (unsigned long)status);

            DeleteSecurityContext(&ctx);
        }

        FreeCredentialsHandle(&cred);
    }

    // Test 4: Use ANSI version
    printf("\n=== Test 4: AcquireCredentialsHandleA ===\n");
    {
        CredHandle cred;
        SecInvalidateHandle(&cred);
        TimeStamp expiry;

        SCHANNEL_CRED cred_data = {};
        cred_data.dwVersion = SCHANNEL_CRED_VERSION;
        cred_data.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

        SECURITY_STATUS status = AcquireCredentialsHandleA(
            nullptr,
            const_cast<LPSTR>(UNISP_NAME_A),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred_data,
            nullptr,
            nullptr,
            &cred,
            &expiry);

        printf("AcquireCredentialsHandleA: %s (0x%08lX)\n", status_to_str(status), (unsigned long)status);

        if (status == SEC_E_OK) {
            CtxtHandle ctx;
            SecInvalidateHandle(&ctx);
            unsigned long attr = 0;

            char out_buf[8192];
            SecBuffer out_sec;
            out_sec.cbBuffer = sizeof(out_buf);
            out_sec.BufferType = SECBUFFER_TOKEN;
            out_sec.pvBuffer = out_buf;

            SecBufferDesc out_desc;
            out_desc.ulVersion = SECBUFFER_VERSION;
            out_desc.cBuffers = 1;
            out_desc.pBuffers = &out_sec;

            status = InitializeSecurityContextA(
                &cred,
                nullptr,
                nullptr,
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM,
                0,
                SECURITY_NATIVE_DREP,
                nullptr,
                0,
                &ctx,
                &out_desc,
                &attr,
                nullptr);

            printf("InitializeSecurityContextA: %s (0x%08lX)\n",
                   status_to_str(status), (unsigned long)status);

            DeleteSecurityContext(&ctx);
        }

        FreeCredentialsHandle(&cred);
    }

    // Test 5: With hostname
    printf("\n=== Test 5: InitializeSecurityContextW WITH hostname ===\n");
    {
        CredHandle cred;
        SecInvalidateHandle(&cred);
        TimeStamp expiry;

        SCHANNEL_CRED cred_data = {};
        cred_data.dwVersion = SCHANNEL_CRED_VERSION;
        cred_data.dwFlags = SCH_CRED_NO_DEFAULT_CREDS;

        SECURITY_STATUS status = AcquireCredentialsHandleW(
            nullptr,
            const_cast<LPWSTR>(UNISP_NAME_W),
            SECPKG_CRED_OUTBOUND,
            nullptr,
            &cred_data,
            nullptr,
            nullptr,
            &cred,
            &expiry);

        printf("AcquireCredentialsHandleW: %s (0x%08lX)\n", status_to_str(status), (unsigned long)status);

        if (status == SEC_E_OK) {
            CtxtHandle ctx;
            SecInvalidateHandle(&ctx);
            unsigned long attr = 0;

            char out_buf[8192];
            SecBuffer out_sec;
            out_sec.cbBuffer = sizeof(out_buf);
            out_sec.BufferType = SECBUFFER_TOKEN;
            out_sec.pvBuffer = out_buf;

            SecBufferDesc out_desc;
            out_desc.ulVersion = SECBUFFER_VERSION;
            out_desc.cBuffers = 1;
            out_desc.pBuffers = &out_sec;

            // 模拟原始代码的主机名转换
            const char* host = "api.openai.com";
            int len = MultiByteToWideChar(CP_UTF8, 0, host, (int)strlen(host), nullptr, 0);
            std::wstring whostname;
            whostname.resize(len);
            MultiByteToWideChar(CP_UTF8, 0, host, (int)strlen(host), &whostname[0], len);
            LPWSTR target_name = const_cast<LPWSTR>(whostname.c_str());

            printf("  host='%s', whostname='%S', target_name='%S'\n", host, whostname.c_str(), target_name);

            status = InitializeSecurityContextW(
                &cred,
                nullptr,
                target_name,  // 传入主机名
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM,
                0,
                SECURITY_NATIVE_DREP,
                nullptr,
                0,
                &ctx,
                &out_desc,
                &attr,
                nullptr);

            printf("InitializeSecurityContextW: %s (0x%08lX)\n",
                   status_to_str(status), (unsigned long)status);
            printf("  out_sec.cbBuffer = %lu\n", out_sec.cbBuffer);

            DeleteSecurityContext(&ctx);
        }

        FreeCredentialsHandle(&cred);
    }

    printf("\n=== All tests complete ===\n");
    return 0;
}
