#pragma once
// shim.ini beside Daytona.exe (all keys optional; see dist/shim.ini).
#include <string>

namespace shim {

struct Config {
    // [Input]
    bool sdl = true;  // SDL3.dll beside the game: PlayStation, Switch and other pads, wheels
    // [Wheel]: joysticks that SDL reports as wheels, or whose name contains `wheel_name`
    std::string wheel_name;
    int steering_axis = 0, gas_axis = 1, brake_axis = 2;
    float steering_scale = 1.0f;  // >1 reaches full lock with less rotation; negative inverts
    int pedal_rest = 0;           // 0 auto, 1 released at the axis maximum, -1 at its minimum
    // Wheel button per Controls bit (Start, Coin, ShiftUp, ShiftDown, View1-4, Gear1-4); -1 none.
    int buttons[12] = {9, 8, 4, 5, 3, 1, 2, 0, 12, 13, 14, 15};
    // [ForceFeedback]
    bool ffb = true;
    float ffb_strength = 1.0f;  // wheels, 0..2
    bool ffb_invert = false;
    float rumble = 1.0f;        // pads, 0..2
};

const Config& config();

}  // namespace shim
