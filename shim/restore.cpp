#include "restore.hpp"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "diff_runs.hpp"
#include "log.hpp"

namespace shim {
namespace {

bool read_file(const std::wstring& path, std::vector<std::uint8_t>& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (256ll << 20);
    if (ok) {
        out.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(f);
    return ok;
}

const IMAGE_NT_HEADERS32* nt_headers(const std::uint8_t* base, std::size_t size) {
    if (size < sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > size)
        return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

}  // namespace

bool restore_original_image() {
    wchar_t self[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const std::wstring patched_path = self;
    const std::wstring original_path = patched_path + L".original";

    std::vector<std::uint8_t> patched, original;
    if (!read_file(original_path, original)) {
        logf("restore: no %ls beside the game; the patched code stays", original_path.c_str());
        return false;
    }
    if (!read_file(patched_path, patched)) {
        logf("restore: cannot read %ls", patched_path.c_str());
        return false;
    }
    const auto* nt_p = nt_headers(patched.data(), patched.size());
    const auto* nt_o = nt_headers(original.data(), original.size());
    const std::size_t headers = nt_p ? nt_p->OptionalHeader.SizeOfHeaders : 0;
    if (nt_p == nullptr || nt_o == nullptr || patched.size() != original.size() || headers == 0 ||
        headers > patched.size() || std::memcmp(patched.data(), original.data(), headers) != 0) {
        logf("restore: Daytona.exe.original does not match this Daytona.exe's layout; nothing changed");
        return false;
    }

    auto* image = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    const IMAGE_DATA_DIRECTORY& iat = nt_p->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    const auto* sec = IMAGE_FIRST_SECTION(nt_p);
    std::size_t runs = 0, bytes = 0, skipped = 0;
    for (unsigned s = 0; s < nt_p->FileHeader.NumberOfSections; ++s, ++sec) {
        const std::size_t raw = sec->PointerToRawData;
        std::size_t len = std::min<std::size_t>(sec->SizeOfRawData, sec->Misc.VirtualSize);
        if (raw >= patched.size()) continue;
        len = std::min(len, patched.size() - raw);
        for (const DiffRun& r : diff_runs(patched.data() + raw, original.data() + raw, len)) {
            const std::uint32_t rva = sec->VirtualAddress + static_cast<std::uint32_t>(r.offset);
            std::uint8_t* at = image + rva;
            // The loader has written the import table; never touch it. And only
            // replace bytes that are still the patched file's.
            const bool in_iat = rva < iat.VirtualAddress + iat.Size && rva + r.length > iat.VirtualAddress;
            if (in_iat || std::memcmp(at, patched.data() + raw + r.offset, r.length) != 0) {
                ++skipped;
                continue;
            }
            DWORD old = 0;
            if (!VirtualProtect(at, r.length, PAGE_EXECUTE_READWRITE, &old)) {
                ++skipped;
                continue;
            }
            std::memcpy(at, original.data() + raw + r.offset, r.length);
            VirtualProtect(at, r.length, old, &old);
            ++runs;
            bytes += r.length;
        }
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    logf("restore: %zu ranges (%zu bytes) put back from Daytona.exe.original, %zu skipped", runs, bytes, skipped);
    return skipped == 0;
}

}  // namespace shim
