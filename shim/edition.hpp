#pragma once
// Which Daytona.exe loaded us, by the PE header's timestamp, and the game
// addresses the shim reads (from daytona-3-nse-decomp game_addresses.hpp).
#include <cstdint>

namespace shim {

enum class Edition { Unknown, Dcu2017, Nse2019 };

struct GameAddresses {
    std::uint32_t shell_credits;  // u8 at +7: credits as the shell last set them (0xFF before the first)
};

Edition detect_edition();
const char* edition_name(Edition e);
const GameAddresses* game_addresses(Edition e);  // null when unknown
// 2017 only: true when this is TeknoParrot's patched executable (its edit at 0x4DD697).
bool teknoparrot_patched();

}  // namespace shim
