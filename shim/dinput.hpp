#pragma once
#include <windows.h>

namespace shim {
// DirectInput8Create's body: an IDirectInput8A whose only game controller is
// the cabinet's SEGA IO board.
HRESULT create_direct_input(REFIID riid, void** out);
}  // namespace shim
