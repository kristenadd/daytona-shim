// dinput8.dll beside Daytona.exe: the cabinet around the original game.
// See PLAN.md and NOTES.md.
#include <windows.h>
#include <unknwn.h>

#include "dinput.hpp"
#include "edition.hpp"
#include "log.hpp"
#include "net.hpp"
#include "pad.hpp"
#include "shell.hpp"

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD version, REFIID riid, LPVOID* out,
                                                                   LPUNKNOWN) {
    if (version != 0x0800) shim::logf("dinput: version 0x%lX asked", static_cast<unsigned long>(version));
    return shim::create_direct_input(riid, out);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID, REFIID, LPVOID* out) {
    if (out != nullptr) *out = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}
extern "C" HRESULT WINAPI DllRegisterServer() { return E_NOTIMPL; }
extern "C" HRESULT WINAPI DllUnregisterServer() { return E_NOTIMPL; }

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    shim::log_open();
    const shim::Edition edition = shim::detect_edition();
    shim::logf("daytona-shim: Daytona.exe edition %s", shim::edition_name(edition));
    if (edition == shim::Edition::Unknown) {
        shim::logf("not a known Daytona.exe (2017 or 2019 PC release); only DirectInput is provided");
        return TRUE;
    }
    if (edition == shim::Edition::Dcu2017 && shim::teknoparrot_patched())
        shim::logf("warning: this is TeknoParrot's patched 2017 Daytona.exe; run Daytona.exe.original instead");
    shim::install_net_hooks();
    // The thread starts once the loader is done with us.
    shim::start_shell(edition);
    return TRUE;
}
