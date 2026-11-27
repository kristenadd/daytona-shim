#pragma once
// Force feedback. The game computes it itself (soc::ForceFeedbackImpl) and
// starts c:\sega\shell\InpWrapper.exe -h=<its window> to drive the cabinet's
// motor. The helper registers its own window by sending the game WM_COPYDATA
// (dwData 3, the window handle), and the game then sends it one-byte commands
// (WM_COPYDATA, dwData 0x20): 0 off, 0x01-0x0F a force of 1-15 one way,
// 0x11-0x1F the other way. The shim plays the helper with a hidden window.
#include <windows.h>

namespace shim {
// Called instead of starting the helper.
void ffb_start(HWND game_window);
// The game's current force, -1 .. 1 (0 until the game sends one).
float ffb_force();
}  // namespace shim
