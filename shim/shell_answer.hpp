#pragma once
// The cabinet shell's answer to the game's security challenge.
//
// Every 10 s the game sends DYT message 0x2A with a 4-byte challenge and
// counts the challenges left unanswered; after five it asks for a reboot and
// exits. The shell answers with 0x2E, whose 4-byte payload run through the
// game's check (2019 0x584600) gives the challenge back.
// Ported from daytona-3-nse-decomp src/recomp/shell.cpp.
#include <array>
#include <cstdint>

namespace shim {

// The game's check, applied to `response`: XOR 0xB1, twelve rounds of a
// 32-bit shift register with feedback from fixed bits, a bit reversal of byte
// 0, XOR 0xC0, XOR 0x77. A response is accepted when this equals the challenge.
std::array<std::uint8_t, 4> shell_check_transform(std::array<std::uint8_t, 4> response);

// The response the check accepts for `challenge`. The transform is affine over
// GF(2) and invertible, so this solves it exactly.
std::array<std::uint8_t, 4> shell_answer(const std::array<std::uint8_t, 4>& challenge);

}  // namespace shim
