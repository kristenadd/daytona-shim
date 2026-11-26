#include "shell.hpp"

#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include "dyt.hpp"
#include "log.hpp"

namespace shim {
namespace {

constexpr u_short kShellPort = 18888;  // 0x49C8, pushed at 2019 0x5847CC / 2017 0x5826CC
const GameAddresses* addresses = nullptr;
std::mutex send_lock;
SOCKET client = INVALID_SOCKET;

bool send_all(SOCKET s, const std::vector<std::uint8_t>& p) {
    std::size_t done = 0;
    while (done < p.size()) {
        const int n = send(s, reinterpret_cast<const char*>(p.data() + done), static_cast<int>(p.size() - done), 0);
        if (n <= 0) return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

void serve(SOCKET s) {
    DytReader reader;
    bool greeted = false;
    unsigned received = 0, challenges = 0;
    std::uint8_t buf[4096];
    for (;;) {
        const int n = recv(s, reinterpret_cast<char*>(buf), sizeof(buf), 0);
        if (n <= 0) {
            logf("shell: link closed (%d)", n == 0 ? 0 : WSAGetLastError());
            return;
        }
        reader.feed(buf, static_cast<std::size_t>(n));
        while (auto p = reader.next()) {
            ++received;
            if (p->id == kMsgSecurityChallenge) ++challenges;
            if (received <= 30 || (p->id == kMsgSecurityChallenge && challenges % 30 == 0))
                logf("shell: <- 0x%02X (%zu bytes)%s", p->id, p->payload.size(),
                     p->id == kMsgSecurityChallenge ? " challenge" : "");
            for (const auto& out : shell_reply(*p, greeted)) {
                std::lock_guard<std::mutex> g(send_lock);
                if (!send_all(s, out)) logf("shell: send failed (%d)", WSAGetLastError());
                else if (received <= 30) logf("shell: -> 0x%02X", out[3] << 8 | out[4]);
            }
        }
    }
}

DWORD WINAPI shell_thread(void*) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        logf("shell: WSAStartup failed");
        return 1;
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(kShellPort);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || listen(listener, 2) != 0) {
        logf("shell: cannot listen on 127.0.0.1:%u (%d). Is Shell.exe or TeknoParrot running?", kShellPort,
             WSAGetLastError());
        return 1;
    }
    logf("shell: listening on 127.0.0.1:%u", kShellPort);
    for (;;) {
        SOCKET s = accept(listener, nullptr, nullptr);
        if (s == INVALID_SOCKET) {
            logf("shell: accept failed (%d)", WSAGetLastError());
            Sleep(100);
            continue;
        }
        logf("shell: game connected");
        {
            std::lock_guard<std::mutex> g(send_lock);
            client = s;
        }
        serve(s);
        {
            std::lock_guard<std::mutex> g(send_lock);
            client = INVALID_SOCKET;
        }
        closesocket(s);
    }
}

}  // namespace

void start_shell(Edition edition) {
    addresses = game_addresses(edition);
    HANDLE t = CreateThread(nullptr, 0, shell_thread, nullptr, 0, nullptr);
    if (t != nullptr) CloseHandle(t);
}

void shell_insert_coin() {
    if (addresses == nullptr) return;
    const std::uint8_t now = *reinterpret_cast<const volatile std::uint8_t*>(addresses->shell_credits + 7u);
    const std::uint8_t credits = static_cast<std::uint8_t>(now == 0xFF ? 1 : std::min(now + 1, 99));
    std::lock_guard<std::mutex> g(send_lock);
    if (client == INVALID_SOCKET) {
        logf("shell: coin before the link is up, ignored");
        return;
    }
    const auto p = dyt_encode(kMsgSetCredit, &credits, 1);
    if (send_all(client, p)) logf("shell: -> coin, credits %u", credits);
}

}  // namespace shim
