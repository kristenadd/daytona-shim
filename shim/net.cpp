// The cabinet network, turned into a lone cabinet's (NOTES.md section 3).
//
// - connect: only loopback connects (the shell link, 127.0.0.1:18888). Every
//   other address is refused at once, as on a cabinet with no others: 2017's
//   discovery probes 192.168.1.2 .. .8:26000, and link play connects to
//   192.168.1.<n>:27000, which on a real network would wait for timeouts.
// - bind: 192.168.x.x becomes 127.0.0.1 (the link-play listener binds the
//   cabinet's own 192.168.1.<n>:27000, which a PC does not have).
// - gethostbyname: the machine's own name resolves to 127.0.0.1, so the
//   address check (2019 0x5775A0) sees no cabinet network.
// - XInputGetState: no pad. The engine reads XInput pads itself as well as
//   the IO board, so a pad there would reach the game twice.
#include "net.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ffb.hpp"
#include "iat.hpp"
#include "log.hpp"

namespace shim {
namespace {

using connect_fn = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using bind_fn = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using gethostbyname_fn = hostent*(WSAAPI*)(const char*);
using gethostname_fn = int(WSAAPI*)(char*, int);
using create_process_fn = BOOL(WINAPI*)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                                        LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);

connect_fn real_connect = nullptr;
bind_fn real_bind = nullptr;
gethostbyname_fn real_gethostbyname = nullptr;
create_process_fn real_create_process = nullptr;

void format_addr(const sockaddr_in* a, char* out, std::size_t n) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(&a->sin_addr);
    std::snprintf(out, n, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], ntohs(a->sin_port));
}

int WSAAPI hook_connect(SOCKET s, const sockaddr* name, int len) {
    if (name != nullptr && name->sa_family == AF_INET && len >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto* a = reinterpret_cast<const sockaddr_in*>(name);
        char text[32];
        format_addr(a, text, sizeof(text));
        if (reinterpret_cast<const std::uint8_t*>(&a->sin_addr)[0] != 127) {
            static LONG shown = 0;
            if (InterlockedIncrement(&shown) <= 20) logf("connect %s: refused (no other cabinets)", text);
            WSASetLastError(WSAECONNREFUSED);
            return SOCKET_ERROR;
        }
        static LONG shown = 0;
        if (InterlockedIncrement(&shown) <= 20) logf("connect %s", text);
    }
    return real_connect(s, name, len);
}

int WSAAPI hook_bind(SOCKET s, const sockaddr* name, int len) {
    if (name != nullptr && name->sa_family == AF_INET && len >= static_cast<int>(sizeof(sockaddr_in))) {
        sockaddr_in a;
        std::memcpy(&a, name, sizeof(a));
        const auto* b = reinterpret_cast<const std::uint8_t*>(&a.sin_addr);
        char text[32];
        format_addr(&a, text, sizeof(text));
        if (b[0] == 192 && b[1] == 168) {
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            const int r = real_bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof(a));
            logf("bind %s -> 127.0.0.1: %s (%d)", text, r == 0 ? "ok" : "failed", r == 0 ? 0 : WSAGetLastError());
            return r;
        }
        const int r = real_bind(s, name, len);
        logf("bind %s: %s (%d)", text, r == 0 ? "ok" : "failed", r == 0 ? 0 : WSAGetLastError());
        return r;
    }
    return real_bind(s, name, len);
}

hostent* WSAAPI hook_gethostbyname(const char* name) {
    char own[256] = {};
    static auto own_name = reinterpret_cast<gethostname_fn>(GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "gethostname"));
    const bool mine = name == nullptr || name[0] == 0 || _stricmp(name, "localhost") == 0 ||
                      (own_name != nullptr && own_name(own, sizeof(own)) == 0 && _stricmp(name, own) == 0);
    if (!mine) return real_gethostbyname(name);
    static char host_name[] = "localhost";
    static std::uint8_t loopback[4] = {127, 0, 0, 1};
    static char* addr_list[2] = {reinterpret_cast<char*>(loopback), nullptr};
    static char* aliases[1] = {nullptr};
    static hostent h{host_name, aliases, AF_INET, 4, addr_list};
    static LONG shown = 0;
    if (InterlockedIncrement(&shown) <= 5) logf("gethostbyname(%s) -> 127.0.0.1", name ? name : "(null)");
    return &h;
}

DWORD WINAPI hook_xinput_get_state(DWORD, void*) { return ERROR_DEVICE_NOT_CONNECTED; }

BOOL WINAPI hook_create_process(LPCSTR app, LPSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta, BOOL inherit,
                                DWORD flags, LPVOID env, LPCSTR dir, LPSTARTUPINFOA si, LPPROCESS_INFORMATION pi) {
    // The force-feedback helper (0x5283E0): "-h=<game window> -p=<port>". The
    // shim takes its place; the game handles the failed start.
    const std::size_t len = app != nullptr ? std::strlen(app) : 0;
    if (len >= 14 && _stricmp(app + len - 14, "InpWrapper.exe") == 0) {
        const char* h = cmd != nullptr ? std::strstr(cmd, "-h=") : nullptr;
        const HWND game = h != nullptr ? reinterpret_cast<HWND>(static_cast<UINT_PTR>(std::strtoul(h + 3, nullptr, 16)))
                                       : nullptr;
        logf("CreateProcessA(%s, %s): the shim is the force-feedback helper", app, cmd ? cmd : "null");
        if (game != nullptr) ffb_start(game);
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    const BOOL r = real_create_process(app, cmd, pa, ta, inherit, flags, env, dir, si, pi);
    logf("CreateProcessA(%s, %s): %s (%lu)", app ? app : "null", cmd ? cmd : "null", r ? "started" : "failed",
         r ? 0ul : static_cast<unsigned long>(GetLastError()));
    return r;
}

}  // namespace

void install_net_hooks() {
    patch_import("WS2_32.dll", nullptr, 4, reinterpret_cast<void*>(hook_connect), reinterpret_cast<void**>(&real_connect));
    patch_import("WS2_32.dll", nullptr, 2, reinterpret_cast<void*>(hook_bind), reinterpret_cast<void**>(&real_bind));
    patch_import("WS2_32.dll", nullptr, 52, reinterpret_cast<void*>(hook_gethostbyname),
                 reinterpret_cast<void**>(&real_gethostbyname));
    patch_import("XINPUT9_1_0.dll", "XInputGetState", 0, reinterpret_cast<void*>(hook_xinput_get_state), nullptr);
    patch_import("KERNEL32.dll", "CreateProcessA", 0, reinterpret_cast<void*>(hook_create_process),
                 reinterpret_cast<void**>(&real_create_process));
}

}  // namespace shim
