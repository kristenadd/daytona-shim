// dinput8.dll beside Daytona.exe: the cabinet around the original game.
// See PLAN.md and NOTES.md.
#include <windows.h>
#include <unknwn.h>

#include "dinput.hpp"
#include "edition.hpp"
#include "log.hpp"
#include "net.hpp"
#include "pad.hpp"
#include "restore.hpp"
#include "shell.hpp"

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD version, REFIID riid, LPVOID* out,
                                                                   LPUNKNOWN) {
    if (version != 0x0800) shim::logf("dinput: version 0x%lX asked", static_cast<unsigned long>(version));
    return shim::create_direct_input(riid, out);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() { return S_FALSE; }
// COM's DirectInput (CoCreateInstance, as SDL uses for wheels) may find this
// DLL by name: hand it the system's.
extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) {
    using get_class_object_fn = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
    static get_class_object_fn system = [] {
        wchar_t path[MAX_PATH];
        const UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (n == 0 || n + 13 >= MAX_PATH) return static_cast<get_class_object_fn>(nullptr);
        wcscat(path, L"\\dinput8.dll");
        HMODULE m = LoadLibraryW(path);
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&DirectInput8Create), &self);
        if (m == nullptr || m == self) return static_cast<get_class_object_fn>(nullptr);
        return reinterpret_cast<get_class_object_fn>(GetProcAddress(m, "DllGetClassObject"));
    }();
    if (system != nullptr) return system(clsid, iid, out);
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
    shim::start_input();
    if (edition == shim::Edition::Unknown) {
        shim::logf("not a known Daytona.exe (2017 or 2019 PC release); only DirectInput is provided");
        return TRUE;
    }
    if (edition == shim::Edition::Dcu2017 && shim::teknoparrot_patched()) {
        shim::logf("this is TeknoParrot's patched 2017 Daytona.exe; restoring the original in memory");
        if (!shim::restore_original_image() || shim::teknoparrot_patched())
            shim::logf("warning: the patched code is still in place; input and the frame gate may not work");
    }
    shim::install_net_hooks();
    // The thread starts once the loader is done with us.
    shim::start_shell(edition);
    return TRUE;
}
