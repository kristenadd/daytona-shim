// Controllers, on a thread of their own (250 Hz):
// - SDL3 (SDL3.dll beside the game): Xbox, PlayStation, Switch and other
//   pads, and steering wheels (raw axes and buttons, shim.ini [Wheel]).
//   Every connected device is merged: the stick or wheel furthest from the
//   centre steers, the pedals and triggers take the largest value, buttons
//   are combined.
// - Without SDL3: XInput pads (on the Deck, Steam Input presents its
//   controls this way).
// - The keyboard while the game's window is in front (read by the caller).
//
// The default pad layout is the recompilation's (src/runtime/pad_mapping.cpp),
// by button position, so it is the same on every brand (shim.ini [Pad]
// changes it):
//
//   steering   left stick X (10% dead zone)    Left / Right
//   gas, brake right / left trigger             Up / Down
//   start      Start / Options / +              Enter
//   coin       Back / Share / -                 5
//   shift      right / left shoulder            A / Z
//   views 1-4  top / left / right / bottom      F1 .. F4
//   quit       Start + Back held for a second   Esc
//
// Force feedback (ffb.hpp): wheels get it as a constant force on the
// steering axis, pads as rumble.
#include "pad.hpp"

#include <windows.h>
#include <xinput.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "config.hpp"
#include "ffb.hpp"
#include "log.hpp"

namespace shim {
namespace {

constexpr float kStickDeadZone = 0.10f;
constexpr ULONGLONG kQuitHoldMs = 1000;

SRWLOCK shared_lock = SRWLOCK_INIT;
Controls shared;  // the devices' merged state, written by the input thread

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

void merge(Controls& into, const Controls& c) {
    if (!c.connected) return;
    into.connected = true;
    if (std::fabs(c.steering) > std::fabs(into.steering)) into.steering = c.steering;
    into.gas = std::max(into.gas, c.gas);
    into.brake = std::max(into.brake, c.brake);
    into.buttons |= c.buttons;
}

// A pad binding's value: an axis as -1..1 (triggers 0..1), a button as 0 or 1.
// `axis` and `button` read the device.
template <typename AxisFn, typename ButtonFn>
Controls apply_pad_bindings(const Config& cfg, float dz, AxisFn axis, ButtonFn button) {
    const auto value = [&](const PadBinding& b) -> float {
        switch (b.kind) {
            case PadBinding::Axis: return axis(b.index);
            case PadBinding::Button: return button(b.index) ? 1.0f : 0.0f;
            default: return 0.0f;
        }
    };
    Controls c;
    c.connected = true;
    c.steering = cfg.pad[0].kind == PadBinding::Axis ? dead_zone(value(cfg.pad[0]), dz) : 0.0f;
    c.gas = std::clamp(value(cfg.pad[1]), 0.0f, 1.0f);
    c.brake = std::clamp(value(cfg.pad[2]), 0.0f, 1.0f);
    for (int bit = 0; bit < 12; ++bit)
        if (value(cfg.pad[3 + bit]) > 0.5f) c.buttons |= 1u << bit;
    return c;
}

// Pads: the game's force is mostly steady pull in corners, with fast
// alternations for grass, kerbs and scraping. A pad shows the alternations on
// the large motor and a little of the steady pull on the small one.
struct Rumble {
    float last = 0, shake = 0;
    void update(float force, float& low, float& high) {
        shake = std::max(shake * 0.92f, std::fabs(force - last) * 2.0f);
        last = force;
        const float gain = config().rumble;
        low = std::clamp(shake * gain, 0.0f, 1.0f);
        high = std::clamp(std::fabs(force) * 0.2f * gain, 0.0f, 1.0f);
    }
};

// --- XInput (no SDL3) -------------------------------------------------------

using xinput_get_state_fn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using xinput_set_state_fn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
xinput_get_state_fn xinput_get_state = nullptr;
xinput_set_state_fn xinput_set_state = nullptr;

bool xinput_load() {
    // The system's XInput, not the game's import (which the shim answers with "no pad").
    for (const wchar_t* dll : {L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll"}) {
        HMODULE m = LoadLibraryW(dll);
        if (m == nullptr) continue;
        xinput_get_state = reinterpret_cast<xinput_get_state_fn>(GetProcAddress(m, "XInputGetState"));
        xinput_set_state = reinterpret_cast<xinput_set_state_fn>(GetProcAddress(m, "XInputSetState"));
        if (xinput_get_state != nullptr) {
            logf("pad: XInput from %ls", dll);
            return true;
        }
    }
    logf("pad: no XInput; keyboard only");
    return false;
}

struct XInputPads {
    bool connected[4] = {};
    Rumble rumble[4];
    WORD sent_low[4] = {}, sent_high[4] = {};

    Controls read() {
        Controls all;
        for (DWORD user = 0; user < 4; ++user) {
            XINPUT_STATE s{};
            const bool on = xinput_get_state(user, &s) == ERROR_SUCCESS;
            if (on != connected[user])
                logf("pad: XInput user %lu %s", static_cast<unsigned long>(user), on ? "connected" : "gone");
            connected[user] = on;
            if (!on) continue;
            const XINPUT_GAMEPAD& g = s.Gamepad;
            const auto axis = [&](int i) -> float {
                switch (kPadAxes[i].sdl) {
                    case SDL_GAMEPAD_AXIS_LEFTX: return g.sThumbLX / 32767.0f;
                    case SDL_GAMEPAD_AXIS_LEFTY: return -g.sThumbLY / 32767.0f;  // SDL's: down is positive
                    case SDL_GAMEPAD_AXIS_RIGHTX: return g.sThumbRX / 32767.0f;
                    case SDL_GAMEPAD_AXIS_RIGHTY: return -g.sThumbRY / 32767.0f;
                    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return g.bLeftTrigger / 255.0f;
                    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return g.bRightTrigger / 255.0f;
                    default: return 0.0f;
                }
            };
            const auto button = [&](int i) { return (g.wButtons & kPadButtons[i].xinput) != 0; };
            const Controls c = apply_pad_bindings(config(), kStickDeadZone, axis, button);
            merge(all, c);
        }
        return all;
    }

    void feedback(float force, bool stop) {
        if (xinput_set_state == nullptr) return;
        for (DWORD user = 0; user < 4; ++user) {
            if (!connected[user]) continue;
            float low = 0, high = 0;
            if (!stop) rumble[user].update(force, low, high);
            XINPUT_VIBRATION v{static_cast<WORD>(low * 65535), static_cast<WORD>(high * 65535)};
            if (v.wLeftMotorSpeed == sent_low[user] && v.wRightMotorSpeed == sent_high[user]) continue;
            xinput_set_state(user, &v);
            sent_low[user] = v.wLeftMotorSpeed;
            sent_high[user] = v.wRightMotorSpeed;
        }
    }
};

// --- SDL3 -------------------------------------------------------------------

#define SDL_FUNCTIONS(X)                                                                                       \
    X(SDL_SetHint) X(SDL_Init) X(SDL_GetError) X(SDL_PollEvent) X(SDL_GetVersion) X(SDL_IsGamepad)             \
    X(SDL_OpenGamepad) X(SDL_CloseGamepad) X(SDL_GetGamepadAxis) X(SDL_GetGamepadButton)                       \
    X(SDL_GetGamepadJoystick) X(SDL_OpenJoystick) X(SDL_CloseJoystick) X(SDL_GetJoystickType)                  \
    X(SDL_GetJoystickName) X(SDL_GetNumJoystickAxes) X(SDL_GetNumJoystickButtons) X(SDL_GetJoystickAxis)       \
    X(SDL_GetJoystickButton) X(SDL_GetJoystickAxisInitialState) X(SDL_RumbleJoystick)                          \
    X(SDL_OpenHapticFromJoystick) X(SDL_GetHapticFeatures) X(SDL_CreateHapticEffect) X(SDL_UpdateHapticEffect) \
    X(SDL_RunHapticEffect) X(SDL_StopHapticEffect) X(SDL_CloseHaptic) X(SDL_SetHapticAutocenter)

struct Sdl {
#define X(name) decltype(&::name) name = nullptr;
    SDL_FUNCTIONS(X)
#undef X

    bool load() {
        // From the game's folder only, never another program's copy on the path.
        wchar_t path[MAX_PATH];
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        wchar_t* slash = n > 0 && n < MAX_PATH ? wcsrchr(path, L'\\') : nullptr;
        if (slash == nullptr) return false;
        wcscpy(slash + 1, L"SDL3.dll");
        HMODULE m = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m == nullptr) {
            logf("pad: no SDL3.dll beside the game; XInput pads only");
            return false;
        }
#define X(name)                                                               \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(m, #name));        \
    if (name == nullptr) {                                                    \
        logf("pad: SDL3.dll has no %s (SDL 3.2 or later is needed)", #name); \
        return false;                                                         \
    }
        SDL_FUNCTIONS(X)
#undef X
        const int v = SDL_GetVersion();
        logf("pad: SDL %d.%d.%d", v / 1000000, v / 1000 % 1000, v % 1000);
        return true;
    }
};
Sdl sdl;

struct Device {
    SDL_JoystickID id = 0;
    SDL_Gamepad* pad = nullptr;  // pads and XInput wheels: SDL's layout
    SDL_Joystick* joy = nullptr;
    bool wheel = false;           // raw axes and buttons from shim.ini [Wheel]
    bool wheel_type = false;      // SDL says it is a wheel
    std::string name;
    int axes = 0, buttons = 0;
    bool gas_high = true, brake_high = true;  // pedal released at the axis maximum
    std::uint32_t buttons_seen = 0;           // for the log
    bool axis_logged[8] = {};
    SDL_Haptic* haptic = nullptr;
    int effect = -1;  // constant force, -1 when the device has none
    Sint16 sent_level = 0;
    Rumble rumble;
    Uint16 sent_low = 0, sent_high = 0;
    ULONGLONG sent_at = 0;
};

bool name_matches(const std::string& name, const std::string& part) {
    if (part.empty()) return false;
    std::string a = name, b = part;
    for (char& ch : a) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (char& ch : b) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return a.find(b) != std::string::npos;
}

bool pedal_rest_high(SDL_Joystick* joy, int axis) {
    const int rest = config().pedal_rest;
    if (rest != 0) return rest > 0;
    Sint16 v = 0;
    if (sdl.SDL_GetJoystickAxisInitialState(joy, axis, &v) && std::abs(v) > 16000) return v > 0;
    return true;  // most wheels' pedals rest at the maximum on Windows
}

float pedal(const Device& d, int axis, bool high) {
    if (axis < 0 || axis >= d.axes) return 0;
    const int v = sdl.SDL_GetJoystickAxis(d.joy, axis);
    const float t = high ? (32767.0f - v) / 65535.0f : (v + 32768.0f) / 65535.0f;
    return std::clamp(t, 0.0f, 1.0f);
}

SDL_HapticEffect constant_force(Sint16 level) {
    SDL_HapticEffect e{};
    e.type = SDL_HAPTIC_CONSTANT;
    e.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
    e.constant.direction.dir[0] = 1;
    e.constant.length = SDL_HAPTIC_INFINITY;
    e.constant.level = level;
    return e;
}

void open_haptics(Device& d) {
    if (!config().ffb) return;
    d.haptic = sdl.SDL_OpenHapticFromJoystick(d.joy);
    if (d.haptic == nullptr) return;
    const Uint32 features = sdl.SDL_GetHapticFeatures(d.haptic);
    if ((features & SDL_HAPTIC_CONSTANT) == 0) {
        sdl.SDL_CloseHaptic(d.haptic);
        d.haptic = nullptr;
        return;
    }
    if (features & SDL_HAPTIC_AUTOCENTER) sdl.SDL_SetHapticAutocenter(d.haptic, 0);  // the game centres it
    const SDL_HapticEffect e = constant_force(0);
    d.effect = sdl.SDL_CreateHapticEffect(d.haptic, &e);
    if (d.effect < 0 || !sdl.SDL_RunHapticEffect(d.haptic, d.effect, 1)) {
        logf("pad: %s: no constant force (%s)", d.name.c_str(), sdl.SDL_GetError());
        d.effect = -1;
        return;
    }
    logf("pad: %s: force feedback (constant force)", d.name.c_str());
}

struct SdlDevices {
    std::vector<Device> list;

    void add(SDL_JoystickID id) {
        Device d;
        d.id = id;
        if (sdl.SDL_IsGamepad(id)) {
            d.pad = sdl.SDL_OpenGamepad(id);
            d.joy = d.pad != nullptr ? sdl.SDL_GetGamepadJoystick(d.pad) : nullptr;
        } else {
            d.joy = sdl.SDL_OpenJoystick(id);
        }
        if (d.joy == nullptr) {
            logf("pad: device %u did not open (%s)", static_cast<unsigned>(id), sdl.SDL_GetError());
            return;
        }
        const char* name = sdl.SDL_GetJoystickName(d.joy);
        d.name = name != nullptr ? name : "?";
        d.axes = sdl.SDL_GetNumJoystickAxes(d.joy);
        d.buttons = sdl.SDL_GetNumJoystickButtons(d.joy);
        d.wheel_type = sdl.SDL_GetJoystickType(d.joy) == SDL_JOYSTICK_TYPE_WHEEL;
        d.wheel = d.pad == nullptr && (d.wheel_type || name_matches(d.name, config().wheel_name));
        if (d.pad == nullptr && !d.wheel) {
            logf("pad: ignoring %s (not a pad or a wheel; shim.ini [Wheel] Name= takes it as a wheel)", d.name.c_str());
            sdl.SDL_CloseJoystick(d.joy);
            return;
        }
        if (d.wheel) {
            d.gas_high = pedal_rest_high(d.joy, config().gas_axis);
            d.brake_high = pedal_rest_high(d.joy, config().brake_axis);
        }
        logf("pad: %s connected as a %s (%d axes, %d buttons)", d.name.c_str(),
             d.wheel ? "wheel" : d.wheel_type ? "wheel with a pad layout" : "pad", d.axes, d.buttons);
        open_haptics(d);
        list.push_back(std::move(d));
    }

    void remove(SDL_JoystickID id) {
        for (auto it = list.begin(); it != list.end(); ++it) {
            if (it->id != id) continue;
            logf("pad: %s gone", it->name.c_str());
            if (it->haptic != nullptr) sdl.SDL_CloseHaptic(it->haptic);
            if (it->pad != nullptr) sdl.SDL_CloseGamepad(it->pad);
            else sdl.SDL_CloseJoystick(it->joy);
            list.erase(it);
            return;
        }
    }

    static Controls read_pad(Device& d) {
        const auto axis = [&](int i) { return sdl.SDL_GetGamepadAxis(d.pad, kPadAxes[i].sdl) / 32767.0f; };
        const auto button = [&](int i) { return sdl.SDL_GetGamepadButton(d.pad, kPadButtons[i].sdl); };
        return apply_pad_bindings(config(), d.wheel_type ? 0.0f : kStickDeadZone, axis, button);
    }

    static Controls read_wheel(Device& d) {
        const Config& cfg = config();
        Controls c;
        c.connected = true;
        if (cfg.steering_axis >= 0 && cfg.steering_axis < d.axes)
            c.steering = std::clamp(sdl.SDL_GetJoystickAxis(d.joy, cfg.steering_axis) / 32767.0f * cfg.steering_scale,
                                    -1.0f, 1.0f);
        c.gas = pedal(d, cfg.gas_axis, d.gas_high);
        c.brake = pedal(d, cfg.brake_axis, d.brake_high);
        for (int bit = 0; bit < 12; ++bit) {
            const int b = cfg.buttons[bit];
            if (b >= 0 && b < d.buttons && sdl.SDL_GetJoystickButton(d.joy, b)) c.buttons |= 1u << bit;
        }
        // For setting up shim.ini: which buttons and axes this wheel uses.
        for (int b = 0; b < std::min(d.buttons, 32); ++b) {
            if ((d.buttons_seen & (1u << b)) == 0 && sdl.SDL_GetJoystickButton(d.joy, b)) {
                d.buttons_seen |= 1u << b;
                logf("pad: %s: button %d pressed", d.name.c_str(), b);
            }
        }
        for (int a = 0; a < std::min(d.axes, 8); ++a) {
            Sint16 start = 0;
            sdl.SDL_GetJoystickAxisInitialState(d.joy, a, &start);
            if (!d.axis_logged[a] && std::abs(sdl.SDL_GetJoystickAxis(d.joy, a) - start) > 20000) {
                d.axis_logged[a] = true;
                logf("pad: %s: axis %d moved", d.name.c_str(), a);
            }
        }
        return c;
    }

    Controls read() {
        Controls all;
        for (Device& d : list) merge(all, d.pad != nullptr ? read_pad(d) : read_wheel(d));
        return all;
    }

    void feedback(float force, bool stop) {
        const ULONGLONG now = GetTickCount64();
        for (Device& d : list) {
            if (d.effect >= 0) {
                const float f = stop ? 0.0f : std::clamp(force * config().ffb_strength, -1.0f, 1.0f);
                const auto level = static_cast<Sint16>(f * 32767.0f);
                if (level == d.sent_level && !stop) continue;
                const SDL_HapticEffect e = constant_force(level);
                sdl.SDL_UpdateHapticEffect(d.haptic, d.effect, &e);
                d.sent_level = level;
                if (stop) sdl.SDL_StopHapticEffect(d.haptic, d.effect);
                continue;
            }
            float low = 0, high = 0;
            if (!stop) d.rumble.update(force, low, high);
            const auto l = static_cast<Uint16>(low * 65535), h = static_cast<Uint16>(high * 65535);
            // Renewed before SDL's duration runs out, and at once on a change.
            const bool same = l == d.sent_low && h == d.sent_high;
            if (same && ((l == 0 && h == 0) || now - d.sent_at < 100)) continue;
            sdl.SDL_RumbleJoystick(d.joy, l, h, 250);
            d.sent_low = l;
            d.sent_high = h;
            d.sent_at = now;
        }
    }
};

bool sdl_start() {
    if (!config().sdl) {
        logf("pad: SDL off in shim.ini");
        return false;
    }
    if (!sdl.load()) return false;
    sdl.SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // Steam's controllers come through Steam Input (as XInput), with the
    // player's Steam layout; SDL's own drivers for them would bypass it.
    sdl.SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0");
    sdl.SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "0");
    if (!sdl.SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_HAPTIC)) {
        logf("pad: SDL_Init failed (%s); XInput pads only", sdl.SDL_GetError());
        return false;
    }
    return true;
}

void quit_now(const char* why) {
    logf("quit: %s", why);
    // Not ExitProcess: the game's threads and its DLLs' detach handlers can
    // hang on the way out. Nothing the game keeps needs a clean exit.
    TerminateProcess(GetCurrentProcess(), 0);
}

DWORD WINAPI input_thread(void*) {
    const bool use_sdl = sdl_start();
    const bool use_xinput = !use_sdl && xinput_load();
    SdlDevices devices;
    XInputPads xinput;
    ULONGLONG held_since = 0;
    for (;;) {
        Controls c;
        if (use_sdl) {
            SDL_Event e;
            while (sdl.SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_JOYSTICK_ADDED) devices.add(e.jdevice.which);
                else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) devices.remove(e.jdevice.which);
            }
            c = devices.read();
        } else if (use_xinput) {
            c = xinput.read();
        }
        AcquireSRWLockExclusive(&shared_lock);
        shared = c;
        ReleaseSRWLockExclusive(&shared_lock);

        const char* quit = nullptr;
        if (our_window_in_front() && key(VK_ESCAPE)) quit = "Esc";
        constexpr std::uint32_t combo = Controls::Start | Controls::Coin;
        const ULONGLONG now = GetTickCount64();
        if ((c.buttons & combo) != combo) held_since = 0;
        else if (held_since == 0) held_since = now;
        else if (now - held_since >= kQuitHoldMs) quit = "Start + Back";

        const float force = ffb_force();
        if (use_sdl) devices.feedback(force, quit != nullptr);
        else if (use_xinput) xinput.feedback(force, quit != nullptr);
        if (quit != nullptr) quit_now(quit);
        Sleep(4);
    }
}

}  // namespace

void start_input() {
    HANDLE t = CreateThread(nullptr, 0, input_thread, nullptr, 0, nullptr);
    if (t != nullptr) CloseHandle(t);
}

Controls read_controls() {
    AcquireSRWLockShared(&shared_lock);
    Controls c = shared;
    ReleaseSRWLockShared(&shared_lock);
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
