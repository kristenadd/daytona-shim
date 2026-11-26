#pragma once
// shim.log beside Daytona.exe. Thread-safe; each line has the time since load.
namespace shim {
void log_open();
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
}  // namespace shim
