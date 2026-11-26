#include "iat.hpp"

#include <windows.h>

#include <cstring>

#include "log.hpp"

namespace shim {

bool patch_import(const char* dll, const char* name, std::uint16_t ordinal, void* replacement, void** original) {
    auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) return false;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name != 0; ++imp) {
        if (_stricmp(reinterpret_cast<const char*>(base + imp->Name), dll) != 0) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            bool match;
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) {
                match = ordinal != 0 && IMAGE_ORDINAL32(names->u1.Ordinal) == ordinal;
            } else {
                auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
                match = name != nullptr && std::strcmp(reinterpret_cast<const char*>(by_name->Name), name) == 0;
            }
            if (!match) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(DWORD), PAGE_READWRITE, &old)) return false;
            if (original != nullptr) *original = reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(replacement));
            VirtualProtect(&slots->u1.Function, sizeof(DWORD), old, &old);
            const auto slot = static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(&slots->u1.Function));
            if (name != nullptr) logf("hook %s!%s at IAT 0x%08lX", dll, name, slot);
            else logf("hook %s!#%u at IAT 0x%08lX", dll, static_cast<unsigned>(ordinal), slot);
            return true;
        }
    }
    logf("hook %s!%s #%u: import not found", dll, name ? name : "", static_cast<unsigned>(ordinal));
    return false;
}

}  // namespace shim
