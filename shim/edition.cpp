#include "edition.hpp"

#include <windows.h>

namespace shim {

namespace {
constexpr DWORD kStamp2017 = 0x5A3B3168;  // Wed Dec 20 2017, the original and the patched file
constexpr DWORD kStamp2019 = 0x5C7F7953;  // Wed Mar  6 2019
constexpr GameAddresses k2017{0x15158F8};
constexpr GameAddresses k2019{0x15278F8};
}  // namespace

Edition detect_edition() {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    if (nt->OptionalHeader.ImageBase != 0x400000) return Edition::Unknown;
    switch (nt->FileHeader.TimeDateStamp) {
        case kStamp2017: return Edition::Dcu2017;
        case kStamp2019: return Edition::Nse2019;
        default: return Edition::Unknown;
    }
}

const char* edition_name(Edition e) {
    switch (e) {
        case Edition::Dcu2017: return "2017";
        case Edition::Nse2019: return "2019";
        default: return "unknown";
    }
}

const GameAddresses* game_addresses(Edition e) {
    switch (e) {
        case Edition::Dcu2017: return &k2017;
        case Edition::Nse2019: return &k2019;
        default: return nullptr;
    }
}

bool teknoparrot_patched() {
    // Original: 0F 85 (jne); TeknoParrot NOPs it.
    const auto* at = reinterpret_cast<const std::uint8_t*>(0x4DD697);
    return at[0] == 0x90 && at[1] == 0x90;
}

}  // namespace shim
