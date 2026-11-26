#include "log.hpp"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace shim {
namespace {
std::FILE* file = nullptr;
std::mutex lock;
ULONGLONG start = 0;
}  // namespace

void log_open() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash == nullptr) return;
    wcscpy(slash + 1, L"shim.log");
    file = _wfopen(path, L"w");
    start = GetTickCount64();
}

void logf(const char* fmt, ...) {
    std::lock_guard<std::mutex> g(lock);
    if (file == nullptr) return;
    const ULONGLONG ms = GetTickCount64() - start;
    std::fprintf(file, "%6llu.%03llu ", ms / 1000, ms % 1000);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(file, fmt, ap);
    va_end(ap);
    std::fputc('\n', file);
    std::fflush(file);
}

}  // namespace shim
