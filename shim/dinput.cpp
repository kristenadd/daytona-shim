// The cabinet's IO board as DirectInput sees it (NOTES.md section 4; ported
// from daytona-3-nse-decomp src/recomp/d3d11.cpp, "DirectInput: the cabinet's
// SEGA IO board").
//
// 2019 0x5DEBA0 enumerates game controllers and keeps a device only when its
// instance name is "SEGA IO Controller"; it enumerates the axes and sets each
// to -1000..1000, sets c_dfDIJoystick and acquires it. Each frame 0x47CDF0
// polls it with GetDeviceState:
//   lX steering, lRx gas, lZ brake (released -1000, full +1000)
//   buttons: 0 start, 8..11 view 1..4, 3 / 4 shift up / down, 1 2 6 7 gear 1..4
// Coin is not on the board: the shell sends credits.
#define DIRECTINPUT_VERSION 0x0800
#include "dinput.hpp"

#include <dinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "log.hpp"
#include "pad.hpp"
#include "shell.hpp"

namespace shim {
namespace {

constexpr char kBoardName[] = "SEGA IO Controller";
constexpr GUID kBoardInstance = {0x5E6A10D0, 0x0001, 0x0001, {0x53, 0x45, 0x47, 0x41, 0x49, 0x4F, 0x00, 0x01}};
constexpr GUID kBoardProduct = {0x5E6A10D1, 0x0001, 0x0001, {0x53, 0x45, 0x47, 0x41, 0x49, 0x4F, 0x00, 0x02}};
constexpr DWORD kBoardType = 0x116;  // DI8DEVTYPE_DRIVING, first person (as the recompilation reports it)
constexpr unsigned kAxes = 4;        // lX, lY, lZ, lRx at offsets 0, 4, 8, 12
constexpr unsigned kButtons = 12;

void once(const char* what) {
    // A method the game was not seen to call: log it the first few times.
    static std::atomic<int> shown{0};
    if (shown++ < 40) logf("dinput: %s", what);
}

void fill_instance(DIDEVICEINSTANCEA* d) {
    const DWORD size = d->dwSize;
    std::memset(d, 0, size);
    d->dwSize = size;
    d->guidInstance = kBoardInstance;
    d->guidProduct = kBoardProduct;
    d->dwDevType = kBoardType;
    std::strcpy(d->tszInstanceName, kBoardName);
    std::strcpy(d->tszProductName, kBoardName);
}

class Board final : public IDirectInputDevice8A {
public:
    // `board` false: another device (keyboard, mouse) that reports nothing.
    explicit Board(bool board) : board_(board) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A || riid == IID_IDirectInputDeviceA ||
            riid == IID_IDirectInputDevice2A || riid == IID_IDirectInputDevice7A) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        if (caps == nullptr || caps->dwSize < offsetof(DIDEVCAPS, dwPOVs) + sizeof(DWORD)) return DIERR_INVALIDPARAM;
        caps->dwFlags = DIDC_ATTACHED | DIDC_POLLEDDEVICE;
        caps->dwDevType = kBoardType;
        caps->dwAxes = kAxes;
        caps->dwButtons = kButtons;
        caps->dwPOVs = 0;
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags) override {
        if (cb == nullptr) return DIERR_INVALIDPARAM;
        if (flags != DIDFT_ALL && (flags & DIDFT_ABSAXIS) == 0) return DI_OK;
        static const char* names[kAxes] = {"X Axis", "Y Axis", "Z Axis", "X Rotation"};
        static const GUID* types[kAxes] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis};
        for (unsigned i = 0; i < kAxes; ++i) {
            DIDEVICEOBJECTINSTANCEA o{};
            o.dwSize = sizeof(o);
            o.guidType = *types[i];
            o.dwOfs = i * 4;
            o.dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i);
            std::strcpy(o.tszName, names[i]);
            if (cb(&o, ref) == DIENUM_STOP) break;
        }
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID prop, LPDIPROPHEADER h) override {
        if (h == nullptr) return DIERR_INVALIDPARAM;
        if (&prop == &DIPROP_RANGE && h->dwSize >= sizeof(DIPROPRANGE)) {
            auto* r = reinterpret_cast<DIPROPRANGE*>(h);
            const unsigned i = axis_of(h);
            r->lMin = min_[i];
            r->lMax = max_[i];
            return DI_OK;
        }
        once("GetProperty of another property");
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID prop, LPCDIPROPHEADER h) override {
        if (h == nullptr) return DIERR_INVALIDPARAM;
        if (&prop == &DIPROP_RANGE && h->dwSize >= sizeof(DIPROPRANGE)) {
            const auto* r = reinterpret_cast<const DIPROPRANGE*>(h);
            if (r->lMin >= r->lMax) return DIERR_INVALIDPARAM;
            for (unsigned i = 0; i < kAxes; ++i)
                if (h->dwHow == DIPH_DEVICE || (h->dwHow == DIPH_BYID && DIDFT_GETINSTANCE(h->dwObj) == i) ||
                    (h->dwHow == DIPH_BYOFFSET && h->dwObj == i * 4)) {
                    min_[i] = r->lMin;
                    max_[i] = r->lMax;
                }
            return DI_OK;
        }
        // Dead zone, saturation, auto-centre: accepted and ignored.
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE Acquire() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
        if (data == nullptr || (board_ && size < sizeof(DIJOYSTATE))) {
            once("GetDeviceState with an unexpected size");
            return DIERR_INVALIDPARAM;
        }
        std::memset(data, 0, size);
        if (!board_) return DI_OK;
        const Controls c = read_controls();
        auto* s = static_cast<DIJOYSTATE*>(data);
        s->lX = axis(0, c.connected ? (c.steering + 1.0f) * 0.5f : 0.5f);
        s->lY = axis(1, 0.5f);
        s->lZ = axis(2, c.connected ? c.brake : 0.0f);
        s->lRx = axis(3, c.connected ? c.gas : 0.0f);
        for (DWORD& pov : s->rgdwPOV) pov = 0xFFFFFFFFu;
        static const struct {
            unsigned button;
            std::uint32_t bit;
        } board[] = {
            {0, Controls::Start},     {8, Controls::View1},     {9, Controls::View2}, {10, Controls::View3},
            {11, Controls::View4},    {3, Controls::ShiftUp},   {4, Controls::ShiftDown},
            {1, Controls::Gear1},     {2, Controls::Gear2},     {6, Controls::Gear3}, {7, Controls::Gear4},
        };
        for (const auto& b : board)
            if (c.buttons & b.bit) s->rgbButtons[b.button] = 0x80;
        // One credit per press.
        const bool coin = (c.buttons & Controls::Coin) != 0;
        if (coin && !coin_held_) shell_insert_coin();
        coin_held_ = coin;
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD n, DWORD) override {
        once("GetDeviceData");
        if (n != nullptr) *n = 0;
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT f) override {
        if (f != nullptr && f->dwDataSize != sizeof(DIJOYSTATE)) logf("dinput: data format of %lu bytes", f->dwDataSize);
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) override {
        once("GetObjectInfo");
        return DIERR_OBJECTNOTFOUND;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA d) override {
        if (d == nullptr || d->dwSize < sizeof(DIDEVICEINSTANCE_DX3A)) return DIERR_INVALIDPARAM;
        fill_instance(d);
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT* e, LPUNKNOWN) override {
        once("CreateEffect (no force feedback)");
        if (e != nullptr) *e = nullptr;
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA, REFGUID) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override {
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE Poll() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) override {
        return DIERR_UNSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA) override { return DIERR_UNSUPPORTED; }

private:
    unsigned axis_of(LPCDIPROPHEADER h) const {
        if (h->dwHow == DIPH_BYID) return std::min<unsigned>(DIDFT_GETINSTANCE(h->dwObj), kAxes - 1);
        if (h->dwHow == DIPH_BYOFFSET) return std::min<unsigned>(h->dwObj / 4, kAxes - 1);
        return 0;
    }
    // t in 0..1 as a position in the axis range.
    LONG axis(unsigned i, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        return static_cast<LONG>(std::lround(min_[i] + static_cast<double>(t) * (static_cast<double>(max_[i]) - min_[i])));
    }

    std::atomic<ULONG> refs_{1};
    LONG min_[kAxes] = {0, 0, 0, 0};
    LONG max_[kAxes] = {65535, 65535, 65535, 65535};
    bool board_;
    bool coin_held_ = false;
};

class Input final : public IDirectInput8A {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
            *out = this;
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN) override {
        if (out == nullptr) return DIERR_INVALIDPARAM;
        // The keyboard or mouse: the game's keyboard source is unused in this
        // release (0x47CB80 returns false); such a device reports nothing.
        const bool board = guid == kBoardInstance || guid == kBoardProduct;
        logf("dinput: CreateDevice(%s)", board ? kBoardName : "another device, reports nothing");
        *out = new Board(board);
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags) override {
        logf("dinput: EnumDevices(type %lu, flags 0x%lX)", type, flags);
        if (cb == nullptr) return DIERR_INVALIDPARAM;
        if (type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL || GET_DIDEVICE_TYPE(type) == DI8DEVTYPE_DRIVING) {
            DIDEVICEINSTANCEA d{};
            d.dwSize = sizeof(d);
            fill_instance(&d);
            cb(&d, ref);
        }
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID guid) override {
        return guid == kBoardInstance ? DI_OK : DI_NOTATTACHED;
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID, LPCSTR, LPGUID) override { return DIERR_DEVICENOTREG; }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR, LPDIACTIONFORMATA, LPDIENUMDEVICESBYSEMANTICSCBA, LPVOID,
                                                     DWORD) override {
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK, LPDICONFIGUREDEVICESPARAMSA, DWORD,
                                               LPVOID) override {
        return DIERR_UNSUPPORTED;
    }

private:
    std::atomic<ULONG> refs_{1};
};

}  // namespace

HRESULT create_direct_input(REFIID riid, void** out) {
    if (out == nullptr) return DIERR_INVALIDPARAM;
    if (riid != IID_IDirectInput8A) {
        logf("dinput: DirectInput8Create for an interface other than IDirectInput8A");
        *out = nullptr;
        return DIERR_NOINTERFACE;
    }
    *out = static_cast<IDirectInput8A*>(new Input());
    logf("dinput: DirectInput8Create");
    return DI_OK;
}

}  // namespace shim
