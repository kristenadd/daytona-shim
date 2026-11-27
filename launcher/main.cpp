// DaytonaLauncher.exe, beside Daytona.exe: the operator settings, the shim's
// controls, and starting the game.
//
//   (no option)  the full-screen, controller-driven launcher under Wine /
//                Proton (the Steam Deck), the desktop window on Windows
//   --big        the full-screen launcher
//   --desktop    the desktop window
//   --play       start the game at once (the edition chosen last)
#include <windows.h>

#include <cwchar>
#include <vector>

#include "settings.hpp"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdline, int show) {
    using namespace launcher;
    const Install in = Install::find();
    const auto has = [&](const wchar_t* opt) { return cmdline != nullptr && std::wcsstr(cmdline, opt) != nullptr; };
    if (has(L"--play")) {
        // The edition chosen last in the full-screen launcher.
        const std::vector<Install> eds = find_editions(in);
        if (!eds.empty()) play(eds[last_edition(eds)]);
        if (in.has_game()) play(in);
    }
    const bool big = has(L"--big") || (running_under_wine() && !has(L"--desktop"));
    return big ? run_big(in) : run_desktop(in, cmdline, show);
}
