#pragma once
// Pad inputs by name, for shim.ini [Pad] and the launcher. Names follow SDL's
// gamepad mapping strings and are button positions: "a" is the bottom face
// button on every pad (Xbox A, PlayStation Cross, Nintendo B).
#include <cstdint>
#include <cstring>

#include <SDL3/SDL_gamepad.h>

namespace shim {

struct PadButtonName {
    const char* name;
    const char* label;
    SDL_GamepadButton sdl;
    std::uint16_t xinput;  // XINPUT_GAMEPAD_* mask, 0 when XInput has none
};

inline constexpr PadButtonName kPadButtons[] = {
    {"a", "A / Cross (bottom)", SDL_GAMEPAD_BUTTON_SOUTH, 0x1000},
    {"b", "B / Circle (right)", SDL_GAMEPAD_BUTTON_EAST, 0x2000},
    {"x", "X / Square (left)", SDL_GAMEPAD_BUTTON_WEST, 0x4000},
    {"y", "Y / Triangle (top)", SDL_GAMEPAD_BUTTON_NORTH, 0x8000},
    {"back", "Back / Share / -", SDL_GAMEPAD_BUTTON_BACK, 0x0020},
    {"start", "Start / Options / +", SDL_GAMEPAD_BUTTON_START, 0x0010},
    {"leftshoulder", "Left bumper", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x0100},
    {"rightshoulder", "Right bumper", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x0200},
    {"leftstick", "Left stick press", SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x0040},
    {"rightstick", "Right stick press", SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x0080},
    {"dpup", "D-pad up", SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0001},
    {"dpdown", "D-pad down", SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0002},
    {"dpleft", "D-pad left", SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0004},
    {"dpright", "D-pad right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0008},
    {"misc1", "Misc / Capture / Mic", SDL_GAMEPAD_BUTTON_MISC1, 0},
    {"paddle1", "Back paddle 1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, 0},
    {"paddle2", "Back paddle 2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, 0},
    {"paddle3", "Back paddle 3", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, 0},
    {"paddle4", "Back paddle 4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, 0},
    {"touchpad", "Touchpad press", SDL_GAMEPAD_BUTTON_TOUCHPAD, 0},
};

struct PadAxisName {
    const char* name;
    const char* label;
    SDL_GamepadAxis sdl;
};

inline constexpr PadAxisName kPadAxes[] = {
    {"leftx", "Left stick X", SDL_GAMEPAD_AXIS_LEFTX},
    {"lefty", "Left stick Y", SDL_GAMEPAD_AXIS_LEFTY},
    {"rightx", "Right stick X", SDL_GAMEPAD_AXIS_RIGHTX},
    {"righty", "Right stick Y", SDL_GAMEPAD_AXIS_RIGHTY},
    {"lefttrigger", "Left trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
    {"righttrigger", "Right trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
};

inline constexpr int kNumPadButtons = sizeof kPadButtons / sizeof kPadButtons[0];
inline constexpr int kNumPadAxes = sizeof kPadAxes / sizeof kPadAxes[0];

struct PadBinding {
    enum Kind : std::uint8_t { None, Button, Axis } kind = None;
    std::uint8_t index = 0;  // into kPadButtons or kPadAxes
    bool operator==(const PadBinding&) const = default;
};

inline PadBinding pad_button(const char* name) {
    for (int i = 0; i < kNumPadButtons; ++i)
        if (std::strcmp(kPadButtons[i].name, name) == 0) return {PadBinding::Button, static_cast<std::uint8_t>(i)};
    return {};
}
inline PadBinding pad_axis(const char* name) {
    for (int i = 0; i < kNumPadAxes; ++i)
        if (std::strcmp(kPadAxes[i].name, name) == 0) return {PadBinding::Axis, static_cast<std::uint8_t>(i)};
    return {};
}
inline PadBinding parse_pad_binding(const char* name) {
    const PadBinding b = pad_button(name);
    return b.kind != PadBinding::None ? b : pad_axis(name);
}
inline const char* pad_binding_name(const PadBinding& b) {
    switch (b.kind) {
        case PadBinding::Button: return kPadButtons[b.index].name;
        case PadBinding::Axis: return kPadAxes[b.index].name;
        default: return "none";
    }
}

// The game's actions, in the order of shim.ini's keys: three analog ones,
// then the Controls bits (Start, Coin, ShiftUp, ShiftDown, View1-4, Gear1-4).
inline constexpr int kNumActions = 15;
inline constexpr const char* kActionKeys[kNumActions] = {
    "Steering", "Gas", "Brake", "Start", "Coin", "ShiftUp", "ShiftDown", "View1",
    "View2", "View3", "View4", "Gear1", "Gear2", "Gear3", "Gear4"};
inline constexpr const char* kActionLabels[kNumActions] = {
    "Steering", "Gas", "Brake", "Start", "Coin", "Shift up", "Shift down", "View 1",
    "View 2", "View 3", "View 4", "Gear 1", "Gear 2", "Gear 3", "Gear 4"};

}  // namespace shim
