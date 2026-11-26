// Controllers: the first connected XInput pad (Xbox pads on Windows; on the
// Deck, Steam Input presents its controls and other pads this way), plus the
// keyboard while the game's window is in front. The layout is the
// recompilation's (src/runtime/pad_mapping.cpp):
//
//   steering   left stick X (10% dead zone)    Left / Right
//   gas, brake right / left trigger             Up / Down
//   start      Start                            Enter
//   coin       Back                             5
//   shift      right / left shoulder            A / Z
//   views 1-4  Y / X / B / A                    F1 .. F4
//
// SDL3 (PlayStation and Switch pads outside Steam) is the next step.
#include "pad.hpp"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <cmath>

#include "log.hpp"

namespace shim {
namespace {

using xinput_get_state_fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
xinput_get_state_fn xinput_get_state = nullptr;
constexpr float kStickDeadZone = 0.10f;

float dead_zone(float v, float dz) {
    v = std::clamp(v, -1.0f, 1.0f);
    const float mag = std::fabs(v);
    if (mag <= dz) return 0.0f;
    return std::copysign((mag - dz) / (1.0f - dz), v);
}

bool our_window_in_front() {
    HWND w = GetForegroundWindow();
    if (w == nullptr) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

bool key(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

void pad_init() {
    // The system's XInput, not the game's import (which the shim answers with "no pad").
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        HMODULE m = LoadLibraryW(dll);
        if (m == nullptr) continue;
        xinput_get_state = reinterpret_cast<xinput_get_state_fn>(GetProcAddress(m, "XInputGetState"));
        if (xinput_get_state != nullptr) {
            logf("pad: XInput from %ls", dll);
            return;
        }
    }
    logf("pad: no XInput; keyboard only");
}

Controls read_controls() {
    static const bool ready = (pad_init(), true);  // not from DllMain: it loads a DLL
    (void)ready;
    Controls c;
    static int last_user = -1;
    if (xinput_get_state != nullptr) {
        for (DWORD user = 0; user < 4; ++user) {
            XINPUT_STATE s{};
            if (xinput_get_state(user, &s) != ERROR_SUCCESS) continue;
            if (static_cast<int>(user) != last_user) {
                logf("pad: XInput user %lu", static_cast<unsigned long>(user));
                last_user = static_cast<int>(user);
            }
            const XINPUT_GAMEPAD& g = s.Gamepad;
            c.connected = true;
            c.steering = dead_zone(g.sThumbLX / 32767.0f, kStickDeadZone);
            c.gas = g.bRightTrigger / 255.0f;
            c.brake = g.bLeftTrigger / 255.0f;
            const auto set = [&](WORD mask, std::uint32_t b) {
                if (g.wButtons & mask) c.buttons |= b;
            };
            set(XINPUT_GAMEPAD_START, Controls::Start);
            set(XINPUT_GAMEPAD_BACK, Controls::Coin);
            set(XINPUT_GAMEPAD_RIGHT_SHOULDER, Controls::ShiftUp);
            set(XINPUT_GAMEPAD_LEFT_SHOULDER, Controls::ShiftDown);
            set(XINPUT_GAMEPAD_Y, Controls::View1);
            set(XINPUT_GAMEPAD_X, Controls::View2);
            set(XINPUT_GAMEPAD_B, Controls::View3);
            set(XINPUT_GAMEPAD_A, Controls::View4);
            break;
        }
    }
    if (our_window_in_front()) {
        const float steer = (key(VK_RIGHT) ? 1.0f : 0.0f) - (key(VK_LEFT) ? 1.0f : 0.0f);
        if (steer != 0) c.steering = steer;
        if (key(VK_UP)) c.gas = 1;
        if (key(VK_DOWN)) c.brake = 1;
        const auto set = [&](int vk, std::uint32_t b) {
            if (key(vk)) c.buttons |= b;
        };
        set(VK_RETURN, Controls::Start);
        set('5', Controls::Coin);
        set('A', Controls::ShiftUp);
        set('Z', Controls::ShiftDown);
        set(VK_F1, Controls::View1);
        set(VK_F2, Controls::View2);
        set(VK_F3, Controls::View3);
        set(VK_F4, Controls::View4);
        c.connected = true;
    }
    return c;
}

}  // namespace shim
