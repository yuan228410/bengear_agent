#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

namespace {

struct WsaInit {
    WsaInit() {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
    }
};

WsaInit init;

} // unnamed namespace
#endif
