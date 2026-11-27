#pragma once
// The player's controls, as the IO board sees them. The input thread keeps
// them current; the board's GetDeviceState reads them once a frame.
#include <cstdint>

namespace shim {

struct Controls {
    bool connected = false;
    float steering = 0;  // -1 full left .. 1 full right
    float gas = 0;       // 0 .. 1
    float brake = 0;     // 0 .. 1
    std::uint32_t buttons = 0;
    enum : std::uint32_t {
        Start = 1u << 0, Coin = 1u << 1, ShiftUp = 1u << 2, ShiftDown = 1u << 3,
        View1 = 1u << 4, View2 = 1u << 5, View3 = 1u << 6, View4 = 1u << 7,
        Gear1 = 1u << 8, Gear2 = 1u << 9, Gear3 = 1u << 10, Gear4 = 1u << 11,
    };
};

// The input thread: pads and wheels (SDL3, or XInput without it), force
// feedback to them, and quitting on Esc (the game's window in front) or
// Start + Back held for a second.
void start_input();
// The latest controls, with the keyboard added while the game is in front.
Controls read_controls();

}  // namespace shim
