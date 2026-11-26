#pragma once
// The player's controls, as the IO board sees them. Read on demand by the
// board's GetDeviceState (once a frame).
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

void pad_init();
Controls read_controls();

}  // namespace shim
