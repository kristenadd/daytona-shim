#include "ffb.hpp"

#include <atomic>

#include "config.hpp"
#include "log.hpp"

namespace shim {
namespace {

std::atomic<int> force{0};  // -15 .. 15
HWND game = nullptr;

int decode(unsigned b) {
    const int n = static_cast<int>(b & 0x0F);
    switch (b & 0xF0) {
        case 0x00: return n;
        case 0x10: return -n;
        default: return 0x7FFF;
    }
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg != WM_COPYDATA) return DefWindowProcW(w, msg, wp, lp);
    const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lp);
    if (cds != nullptr && cds->dwData == 0x20 && cds->cbData >= 1 && cds->lpData != nullptr) {
        const unsigned b = *static_cast<const unsigned char*>(cds->lpData);
        const int v = decode(b);
        static LONG shown = 0;
        if (v == 0x7FFF) {
            if (InterlockedIncrement(&shown) <= 20) logf("ffb: unknown command 0x%02X", b);
        } else {
            static LONG first = 0;
            if (v != 0 && InterlockedExchange(&first, 1) == 0) logf("ffb: first force from the game (0x%02X)", b);
            force.store(config().ffb_invert ? -v : v, std::memory_order_relaxed);
        }
    }
    return 1;  // the game's "done"; 2 would make it send again
}

DWORD WINAPI helper(void*) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DaytonaShimForceFeedback";
    RegisterClassW(&wc);
    HWND self = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (self == nullptr) {
        logf("ffb: no helper window (%lu)", GetLastError());
        return 0;
    }
    // The game handles this on its own thread when it next pumps messages
    // (0x4DC9D0 in 2019): it stores our window and sends a first 0.
    DWORD handle = static_cast<DWORD>(reinterpret_cast<UINT_PTR>(self));
    COPYDATASTRUCT cds{3, sizeof handle, &handle};
    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(game, WM_COPYDATA, reinterpret_cast<WPARAM>(self),
                                             reinterpret_cast<LPARAM>(&cds), SMTO_NORMAL, 120000, &result);
    logf("ffb: registered with the game window: %s", sent ? "yes" : "no answer");
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) DispatchMessageW(&m);
    return 0;
}

}  // namespace

void ffb_start(HWND game_window) {
    if (!config().ffb) {
        logf("ffb: off in shim.ini");
        return;
    }
    if (game != nullptr) return;
    game = game_window;
    HANDLE t = CreateThread(nullptr, 0, helper, nullptr, 0, nullptr);
    if (t != nullptr) CloseHandle(t);
}

float ffb_force() { return force.load(std::memory_order_relaxed) / 15.0f; }

}  // namespace shim
