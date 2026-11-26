#pragma once
// Import address table patches in the game's executable. Daytona.exe has
// intact imports and no relocations, and the loader has bound them all before
// our DllMain runs, so swapping a slot redirects every call through it.
#include <cstdint>

namespace shim {
// `name` matches an import by name; `ordinal` (non-zero) one by ordinal
// (WS2_32 is imported by ordinal). Writes the old target to `original`.
// Returns false when the import is not there.
bool patch_import(const char* dll, const char* name, std::uint16_t ordinal, void* replacement, void** original);
}  // namespace shim
