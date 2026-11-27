// DaytonaLauncher.exe, beside Daytona.exe: the operator settings
// (ShellData.ini, GameSettings.ini), the shim's controls (shim.ini), and a
// Play button that starts the game. "DaytonaLauncher.exe --play" starts the
// game at once; --page=rules|controller|wheel opens on that tab.
//
// A plain Win32 window, so it runs the same under Proton. SDL3.dll (shipped
// beside it for the shim) is used only to detect pad and wheel inputs.
#include <windows.h>
#include <commctrl.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bindings.hpp"
#include "config.hpp"

using shim::Config;
using shim::PadBinding;

namespace {

// --- Paths and files --------------------------------------------------------

std::wstring exe_dir() {
    wchar_t path[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".";
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash != nullptr) *slash = 0;
    return path;
}

std::wstring full_path(const std::wstring& p) {
    wchar_t out[MAX_PATH];
    const DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH, out, nullptr);
    return n > 0 && n < MAX_PATH ? out : p;
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

enum class Edition { Unknown, Dcu2017, Nse2019 };

Edition edition_of(const std::wstring& exe) {
    HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return Edition::Unknown;
    unsigned char head[4096] = {};
    DWORD got = 0;
    ReadFile(f, head, sizeof head, &got, nullptr);
    CloseHandle(f);
    if (got < 0x40) return Edition::Unknown;
    const auto pe = *reinterpret_cast<const DWORD*>(head + 0x3C);
    if (pe + 12 > got) return Edition::Unknown;
    switch (*reinterpret_cast<const DWORD*>(head + pe + 8)) {
        case 0x5A3B3168: return Edition::Dcu2017;
        case 0x5C7F7953: return Edition::Nse2019;
        default: return Edition::Unknown;
    }
}

std::wstring ini_get(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def = L"") {
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(section, key, def, buf, 256, file.c_str());
    return buf;
}

// The game's own files: a copy is kept as <file>.shim-backup before the
// launcher first changes one.
bool ini_put(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value) {
    const std::wstring backup = file + L".shim-backup";
    if (exists(file) && !exists(backup)) CopyFileW(file.c_str(), backup.c_str(), TRUE);
    return WritePrivateProfileStringW(section, key, value.c_str(), file.c_str()) != 0;
}

std::wstring widen(const char* s) {
    std::wstring w;
    while (*s) w += static_cast<wchar_t>(static_cast<unsigned char>(*s++));
    return w;
}
std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? static_cast<char>(c) : '?';
    return s;
}

// --- The game's rules (GameSettings.ini [GameSettings]) ----------------------
// The game's own table (2019 0x118A510), its default first.

struct Rule {
    const wchar_t* key;
    std::vector<const wchar_t*> options;
};

const std::vector<Rule>& rules() {
    static const std::vector<Rule> r = {
        {L"DIFFICULTY", {L"MEDIUM", L"VERY EASY", L"EASY", L"HARD", L"VERY HARD"}},
        {L"GAME LENGTH", {L"MEDIUM", L"VERY SHORT", L"SHORT", L"LONG", L"VERY LONG", L"CLASSIC MODE"}},
        {L"CAMERA ENABLED", {L"YES", L"NO"}},
        {L"INSTANT REMATCH", {L"YES", L"NO"}},
        {L"START ON COIN ENTRY", {L"NO", L"YES"}},
        {L"MENU TIMINGS", {L"MEDIUM", L"SHORT", L"LONG"}},
        {L"CAR SELECT", {L"CHOOSE", L"RANDOM", L"1", L"2", L"3", L"4", L"5"}},
        {L"TRACK SELECT",
         {L"CHOOSE", L"RANDOM", L"EASY ONLY", L"37_SPEED", L"DINOSAUR", L"SEASIDE", L"DAYTONA", L"LAKESIDE", L"METRO"}},
        {L"TRANSMISSION SELECT", {L"ON", L"OFF"}},
        {L"CHAMPIONSHIP MODE", {L"YES", L"NO"}},
        {L"CHAMPIONSHIP CONTINUE", {L"YES", L"NO"}},
        {L"AUTO DRIVE", {L"YES", L"NO"}},
        {L"KIDS ONLY MODE", {L"NO", L"YES"}},
        {L"TOURNAMENT MODE", {L"OFF", L"ON", L"EXCLUSIVE"}},
        {L"TOURNAMENT DURATION",
         {L"14", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"10", L"11", L"12", L"13", L"15",
          L"16", L"17", L"18", L"19", L"20", L"21", L"22", L"23", L"24", L"25", L"26", L"27", L"28", L"29", L"30"}},
        {L"TOURNAMENT TYPE", {L"CUSTOM", L"CHAMPIONSHIP"}},
        {L"CUSTOM TOURNAMENT CAR", {L"1", L"2", L"3", L"4", L"5"}},
        {L"CUSTOM TOURNAMENT TRACK", {L"37_SPEED", L"DINOSAUR", L"SEASIDE", L"DAYTONA", L"LAKESIDE", L"METRO"}},
        {L"CUSTOM TOURNAMENT TRANSMISSION", {L"CHOOSE", L"MANUAL", L"AUTOMATIC"}},
        {L"TOURNAMENT CONTINUE", {L"NO", L"YES"}},
        {L"TOURNAMENT DATE FORMAT", {L"MM/DD/YY", L"YY/MM/DD", L"DD/MM/YY"}},
        {L"DRESS CODE", {L"OFF", L"ON"}},
        {L"DEFAULT CAMERA", {L"3", L"1", L"2", L"4"}},
        {L"LANGUAGE", {L"ENGLISH", L"FRENCH", L"ITALIAN", L"SPANISH", L"RUSSIAN", L"TURKISH", L"PORTUGUESE"}},
    };
    return r;
}

// --- SDL3 (input detection only) ---------------------------------------------

#define LAUNCHER_SDL(X)                                                                                      \
    X(SDL_SetHint) X(SDL_Init) X(SDL_Quit) X(SDL_PollEvent) X(SDL_IsGamepad) X(SDL_OpenGamepad)              \
    X(SDL_CloseGamepad) X(SDL_GetGamepadAxis) X(SDL_GetGamepadButton) X(SDL_GetGamepadJoystick)              \
    X(SDL_OpenJoystick) X(SDL_CloseJoystick) X(SDL_GetJoystickName) X(SDL_GetNumJoystickAxes)                \
    X(SDL_GetNumJoystickButtons) X(SDL_GetJoystickAxis) X(SDL_GetJoystickButton) X(SDL_GetJoystickType)      \
    X(SDL_GetJoystickAxisInitialState)

struct Sdl {
#define X(name) decltype(&::name) name = nullptr;
    LAUNCHER_SDL(X)
#undef X
    bool ok = false;

    void start(const std::wstring& dir) {
        HMODULE m = LoadLibraryExW((dir + L"\\SDL3.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m == nullptr) return;
#define X(name)                                                        \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(m, #name)); \
    if (name == nullptr) return;
        LAUNCHER_SDL(X)
#undef X
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "0");  // as the shim: Steam Input's layout
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "0");
        ok = SDL_Init(SDL_INIT_GAMEPAD);
    }
};
Sdl sdl;

struct Device {
    SDL_JoystickID id = 0;
    SDL_Gamepad* pad = nullptr;
    SDL_Joystick* joy = nullptr;
    bool wheel = false;  // read raw, as the shim reads wheels
    std::string name;
};
std::vector<Device> devices;

void device_added(SDL_JoystickID id, const std::string& wheel_name) {
    Device d;
    d.id = id;
    if (sdl.SDL_IsGamepad(id)) {
        d.pad = sdl.SDL_OpenGamepad(id);
        d.joy = d.pad ? sdl.SDL_GetGamepadJoystick(d.pad) : nullptr;
    } else {
        d.joy = sdl.SDL_OpenJoystick(id);
    }
    if (d.joy == nullptr) return;
    const char* n = sdl.SDL_GetJoystickName(d.joy);
    d.name = n ? n : "?";
    std::string lower = d.name, part = wheel_name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : part) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    d.wheel = d.pad == nullptr && (sdl.SDL_GetJoystickType(d.joy) == SDL_JOYSTICK_TYPE_WHEEL ||
                                   (!part.empty() && lower.find(part) != std::string::npos));
    devices.push_back(d);
}

void device_removed(SDL_JoystickID id) {
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (it->id != id) continue;
        if (it->pad) sdl.SDL_CloseGamepad(it->pad);
        else sdl.SDL_CloseJoystick(it->joy);
        devices.erase(it);
        return;
    }
}

// --- The window -------------------------------------------------------------

enum Page { PageGame, PageRules, PagePad, PageWheel, NumPages };

struct Detect {
    enum Kind { None, Pad, WheelButton, WheelAxis } kind = None;
    int row = 0;  // pad: action; wheel button: bit; wheel axis: 0 steering, 1 gas, 2 brake
    HWND button = nullptr;
    ULONGLONG started = 0;
    // Baselines when detection began.
    std::vector<std::vector<int>> axes;
    std::vector<std::vector<bool>> buttons;
};

struct App {
    HWND wnd = nullptr, tab = nullptr, status = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    std::vector<HWND> page[NumPages];
    std::wstring dir, game_exe, shelldata, gamesettings;
    Edition edition = Edition::Unknown;
    Config cfg;

    // Game page
    HWND freeplay = nullptr;
    HWND volume[3] = {};
    HWND volume_label[3] = {};
    bool has_audio = false;
    // Rules page
    std::vector<HWND> rule_combo;
    std::vector<int> rule_initial;
    std::vector<bool> rule_in_file;
    // Pad page
    HWND sdl_box = nullptr, pad_devices = nullptr;
    HWND pad_combo[shim::kNumActions] = {};
    HWND pad_detect[shim::kNumActions] = {};
    // Wheel page
    HWND wheel_name = nullptr, wheel_live = nullptr, scale = nullptr, rest = nullptr;
    HWND axis_edit[3] = {}, axis_detect[3] = {};
    HWND button_edit[12] = {}, button_detect[12] = {};
    HWND ffb_box = nullptr, invert_box = nullptr, strength = nullptr, rumble = nullptr;
    HWND strength_label = nullptr, rumble_label = nullptr;

    Detect detect;
    int next_id = 100;
    ULONGLONG live_at = 0;
};
App app;

int S(int v) { return MulDiv(v, app.dpi, 96); }

HWND add(Page p, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | style, S(x), S(y), S(w), S(h), app.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(app.next_id++)), GetModuleHandleW(nullptr),
                             nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    app.page[p].push_back(c);
    return c;
}
HWND label(Page p, const wchar_t* text, int x, int y, int w, int h = 20) {
    return add(p, L"STATIC", text, SS_LEFT, x, y + 3, w, h);
}
HWND combo(Page p, int x, int y, int w) {
    return add(p, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x, y, w, 300);
}
HWND button(Page p, const wchar_t* text, int x, int y, int w, DWORD style = BS_PUSHBUTTON) {
    return add(p, L"BUTTON", text, style | WS_TABSTOP, x, y, w, 24);
}
HWND edit(Page p, const wchar_t* text, int x, int y, int w) {
    return add(p, L"EDIT", text, ES_AUTOHSCROLL | WS_TABSTOP, x, y, w, 23, WS_EX_CLIENTEDGE);
}
HWND slider(Page p, int x, int y, int w, int lo, int hi, int value) {
    HWND s = add(p, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, x, y, w, 26);
    SendMessageW(s, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageW(s, TBM_SETPOS, TRUE, value);
    return s;
}

std::wstring text_of(HWND h) {
    wchar_t buf[256] = {};
    GetWindowTextW(h, buf, 256);
    return buf;
}
int slider_pos(HWND s) { return static_cast<int>(SendMessageW(s, TBM_GETPOS, 0, 0)); }
bool checked(HWND b) { return SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void set_check(HWND b, bool on) { SendMessageW(b, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0); }

void set_status(const std::wstring& s) { SetWindowTextW(app.status, s.c_str()); }

void show_page(int p) {
    for (int i = 0; i < NumPages; ++i)
        for (HWND h : app.page[i]) ShowWindow(h, i == p ? SW_SHOW : SW_HIDE);
}

// Pad combo entries: none, the buttons, then the axes.
int pad_combo_index(const PadBinding& b) {
    switch (b.kind) {
        case PadBinding::Button: return 1 + b.index;
        case PadBinding::Axis: return 1 + shim::kNumPadButtons + b.index;
        default: return 0;
    }
}
PadBinding pad_binding_at(int i) {
    if (i >= 1 && i <= shim::kNumPadButtons) return {PadBinding::Button, static_cast<std::uint8_t>(i - 1)};
    if (i > shim::kNumPadButtons && i <= shim::kNumPadButtons + shim::kNumPadAxes)
        return {PadBinding::Axis, static_cast<std::uint8_t>(i - 1 - shim::kNumPadButtons)};
    return {};
}

void update_percent_labels() {
    for (int i = 0; i < 3; ++i)
        if (app.volume[i]) SetWindowTextW(app.volume_label[i], std::to_wstring(slider_pos(app.volume[i])).c_str());
    SetWindowTextW(app.strength_label, (std::to_wstring(slider_pos(app.strength)) + L"%").c_str());
    SetWindowTextW(app.rumble_label, (std::to_wstring(slider_pos(app.rumble)) + L"%").c_str());
}

void build_game_page() {
    const bool known = app.edition != Edition::Unknown;
    std::wstring head = L"Edition: ";
    head += app.edition == Edition::Nse2019   ? L"Daytona Championship USA New Season Edition (2019)"
            : app.edition == Edition::Dcu2017 ? L"Daytona Championship USA (2017)"
                                              : L"unknown (Daytona.exe not found or not a known release)";
    label(PageGame, head.c_str(), 24, 50, 700);
    label(PageGame, (L"Settings: " + app.shelldata).c_str(), 24, 74, 700);
    app.freeplay = button(PageGame, L"Free play (off: insert coins with the Coin button)", 24, 110, 420, BS_AUTOCHECKBOX);
    set_check(app.freeplay, ini_get(app.shelldata, L"Credit", L"Freeplay", L"1") != L"0");
    app.has_audio = !ini_get(app.shelldata, L"Audio", L"MusicVolume").empty();
    if (app.has_audio) {
        static const wchar_t* const names[3] = {L"Music volume", L"Effects volume", L"Voice volume"};
        static const wchar_t* const keys[3] = {L"MusicVolume", L"SFXVolume", L"VoiceVolume"};
        for (int i = 0; i < 3; ++i) {
            const int y = 150 + i * 36;
            label(PageGame, names[i], 24, y, 140);
            app.volume[i] = slider(PageGame, 170, y, 300, 0, 100, _wtoi(ini_get(app.shelldata, L"Audio", keys[i], L"100").c_str()));
            app.volume_label[i] = label(PageGame, L"", 480, y, 50);
        }
    }
    label(PageGame,
          L"The first time the launcher changes one of the game's files it keeps a copy beside it "
          L"(.shim-backup).",
          24, 280, 700, 40);
    if (!known) EnableWindow(app.freeplay, FALSE);
}

void build_rules_page() {
    label(PageRules, L"The game's rules (GameSettings.ini). The first option of each list is the game's default.", 24,
          44, 720);
    if (app.edition == Edition::Dcu2017)
        label(PageRules, L"2017: whether this edition reads these has not been checked.", 24, 64, 720);
    const auto& r = rules();
    for (std::size_t i = 0; i < r.size(); ++i) {
        const int col = static_cast<int>(i) / 12, row = static_cast<int>(i) % 12;
        const int x = 24 + col * 370, y = 90 + row * 30;
        std::wstring name = r[i].key;
        for (std::size_t k = 1; k < name.size(); ++k)
            if (name[k - 1] != L' ') name[k] = static_cast<wchar_t>(towlower(name[k]));
        label(PageRules, name.c_str(), x, y, 190);
        HWND c = combo(PageRules, x + 195, y, 155);
        for (const wchar_t* o : r[i].options) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(o));
        const std::wstring value = ini_get(app.gamesettings, L"GameSettings", r[i].key);
        int sel = 0;
        for (std::size_t k = 0; k < r[i].options.size(); ++k)
            if (value == r[i].options[k]) sel = static_cast<int>(k);
        SendMessageW(c, CB_SETCURSEL, sel, 0);
        app.rule_combo.push_back(c);
        app.rule_initial.push_back(sel);
        app.rule_in_file.push_back(!value.empty());
    }
}

void build_pad_page() {
    app.sdl_box = button(PagePad, L"Use SDL3 (PlayStation, Switch and other pads, wheels); off: Xbox pads only",
                         24, 44, 560, BS_AUTOCHECKBOX);
    set_check(app.sdl_box, app.cfg.sdl);
    app.pad_devices = label(PagePad, L"", 24, 70, 720);
    label(PagePad, L"Buttons go by position, so the layout is the same on every brand.", 24, 92, 720);
    for (int i = 0; i < shim::kNumActions; ++i) {
        const int col = i < 8 ? 0 : 1, row = i < 8 ? i : i - 8;
        const int x = 24 + col * 370, y = 120 + row * 32;
        label(PagePad, widen(shim::kActionLabels[i]).c_str(), x, y, 80);
        HWND c = combo(PagePad, x + 85, y, 180);
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none)"));
        for (const auto& b : shim::kPadButtons) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(b.label).c_str()));
        for (const auto& a : shim::kPadAxes) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(a.label).c_str()));
        SendMessageW(c, CB_SETCURSEL, pad_combo_index(app.cfg.pad[i]), 0);
        app.pad_combo[i] = c;
        app.pad_detect[i] = button(PagePad, L"Detect", x + 272, y, 70);
    }
    label(PagePad, L"Quit the game: hold Start and Coin for a second, or press Esc.", 24, 384, 720);
    button(PagePad, L"Default layout", 24, 410, 130);
}

void build_wheel_page() {
    label(PageWheel, L"Wheel name (only for wheels SDL does not know)", 24, 44, 300);
    app.wheel_name = edit(PageWheel, widen(app.cfg.wheel_name.c_str()).c_str(), 24, 66, 200);
    app.wheel_live = label(PageWheel, L"", 24, 96, 340, 40);
    static const wchar_t* const axis_names[3] = {L"Steering axis", L"Gas axis", L"Brake axis"};
    const int axis_values[3] = {app.cfg.steering_axis, app.cfg.gas_axis, app.cfg.brake_axis};
    for (int i = 0; i < 3; ++i) {
        const int y = 144 + i * 32;
        label(PageWheel, axis_names[i], 24, y, 110);
        app.axis_edit[i] = edit(PageWheel, std::to_wstring(axis_values[i]).c_str(), 140, y, 50);
        app.axis_detect[i] = button(PageWheel, L"Detect", 200, y, 70);
    }
    label(PageWheel, L"Steering scale", 24, 244, 110);
    wchar_t scale[32];
    std::swprintf(scale, 32, L"%.2f", app.cfg.steering_scale);
    app.scale = edit(PageWheel, scale, 140, 244, 50);
    label(PageWheel, L"2: full lock at half the travel; negative: reversed", 200, 244, 170, 40);
    label(PageWheel, L"Pedals at rest", 24, 290, 110);
    app.rest = combo(PageWheel, 140, 290, 130);
    for (const wchar_t* o : {L"auto", L"high", L"low"}) SendMessageW(app.rest, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(o));
    SendMessageW(app.rest, CB_SETCURSEL, app.cfg.pedal_rest > 0 ? 1 : app.cfg.pedal_rest < 0 ? 2 : 0, 0);

    app.ffb_box = button(PageWheel, L"Force feedback", 24, 330, 150, BS_AUTOCHECKBOX);
    set_check(app.ffb_box, app.cfg.ffb);
    app.invert_box = button(PageWheel, L"Reverse it", 190, 330, 150, BS_AUTOCHECKBOX);
    set_check(app.invert_box, app.cfg.ffb_invert);
    label(PageWheel, L"Wheel strength", 24, 362, 110);
    app.strength = slider(PageWheel, 140, 362, 170, 0, 200, static_cast<int>(app.cfg.ffb_strength * 100 + 0.5f));
    app.strength_label = label(PageWheel, L"", 316, 362, 50);
    label(PageWheel, L"Pad rumble", 24, 394, 110);
    app.rumble = slider(PageWheel, 140, 394, 170, 0, 200, static_cast<int>(app.cfg.rumble * 100 + 0.5f));
    app.rumble_label = label(PageWheel, L"", 316, 394, 50);

    label(PageWheel, L"Wheel buttons (numbers; -1: none)", 400, 44, 300);
    static const wchar_t* const names[12] = {L"Start", L"Coin", L"Shift up", L"Shift down", L"View 1", L"View 2",
                                             L"View 3", L"View 4", L"Gear 1", L"Gear 2", L"Gear 3", L"Gear 4"};
    for (int i = 0; i < 12; ++i) {
        const int y = 70 + i * 30;
        label(PageWheel, names[i], 400, y, 90);
        app.button_edit[i] = edit(PageWheel, std::to_wstring(app.cfg.buttons[i]).c_str(), 495, y, 50);
        app.button_detect[i] = button(PageWheel, L"Detect", 555, y, 70);
    }
    button(PageWheel, L"Defaults (Logitech G29 / G923)", 400, 434, 225);
}

Config config_from_page() {
    Config c = app.cfg;
    c.sdl = checked(app.sdl_box);
    for (int i = 0; i < shim::kNumActions; ++i)
        c.pad[i] = pad_binding_at(static_cast<int>(SendMessageW(app.pad_combo[i], CB_GETCURSEL, 0, 0)));
    c.wheel_name = narrow(text_of(app.wheel_name));
    c.steering_axis = _wtoi(text_of(app.axis_edit[0]).c_str());
    c.gas_axis = _wtoi(text_of(app.axis_edit[1]).c_str());
    c.brake_axis = _wtoi(text_of(app.axis_edit[2]).c_str());
    c.steering_scale = static_cast<float>(_wtof(text_of(app.scale).c_str()));
    if (c.steering_scale == 0) c.steering_scale = 1;
    const auto r = SendMessageW(app.rest, CB_GETCURSEL, 0, 0);
    c.pedal_rest = r == 1 ? 1 : r == 2 ? -1 : 0;
    for (int i = 0; i < 12; ++i) {
        const std::wstring t = text_of(app.button_edit[i]);
        c.buttons[i] = t.empty() ? -1 : _wtoi(t.c_str());
    }
    c.ffb = checked(app.ffb_box);
    c.ffb_invert = checked(app.invert_box);
    c.ffb_strength = slider_pos(app.strength) / 100.0f;
    c.rumble = slider_pos(app.rumble) / 100.0f;
    return c;
}

bool save() {
    bool ok = true;
    if (app.edition != Edition::Unknown) {
        ok = ini_put(app.shelldata, L"Credit", L"Freeplay", checked(app.freeplay) ? L"1" : L"0") && ok;
        static const wchar_t* const keys[3] = {L"MusicVolume", L"SFXVolume", L"VoiceVolume"};
        if (app.has_audio)
            for (int i = 0; i < 3; ++i) ok = ini_put(app.shelldata, L"Audio", keys[i], std::to_wstring(slider_pos(app.volume[i]))) && ok;
        // A rule goes into the file when it is there already or was changed.
        const auto& r = rules();
        for (std::size_t i = 0; i < r.size(); ++i) {
            const int sel = static_cast<int>(SendMessageW(app.rule_combo[i], CB_GETCURSEL, 0, 0));
            if (sel < 0 || (!app.rule_in_file[i] && sel == app.rule_initial[i])) continue;
            ok = ini_put(app.gamesettings, L"GameSettings", r[i].key, r[i].options[sel]) && ok;
            app.rule_in_file[i] = true;
        }
    }
    app.cfg = config_from_page();
    ok = shim::save_config(app.cfg, shim::config_path()) && ok;
    set_status(ok ? L"Saved." : L"Some settings could not be written (is the folder read-only?).");
    return ok;
}

void play() {
    if (!exists(app.game_exe)) {
        MessageBoxW(app.wnd, L"Daytona.exe is not beside the launcher.", L"Daytona", MB_ICONERROR);
        return;
    }
    if (app.wnd != nullptr) {
        save();
        ShowWindow(app.wnd, SW_HIDE);
    }
    if (sdl.ok) {
        for (Device& d : devices) {
            if (d.pad) sdl.SDL_CloseGamepad(d.pad);
            else sdl.SDL_CloseJoystick(d.joy);
        }
        devices.clear();
        sdl.SDL_Quit();
        sdl.ok = false;
    }
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + app.game_exe + L"\"";
    if (!CreateProcessW(app.game_exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, app.dir.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"Daytona.exe did not start.", L"Daytona", MB_ICONERROR);
        ExitProcess(1);
    }
    // Stay until the game ends, so Steam sees one session.
    WaitForSingleObject(pi.hProcess, INFINITE);
    ExitProcess(0);
}

// --- Detection ----------------------------------------------------------------

void detect_stop(const wchar_t* message = nullptr) {
    if (app.detect.button) SetWindowTextW(app.detect.button, L"Detect");
    app.detect = Detect{};
    if (message) set_status(message);
}

void detect_start(Detect::Kind kind, int row, HWND b) {
    if (!sdl.ok) {
        set_status(L"Detecting needs SDL3.dll beside the launcher.");
        return;
    }
    detect_stop();
    app.detect.kind = kind;
    app.detect.row = row;
    app.detect.button = b;
    app.detect.started = GetTickCount64();
    for (const Device& d : devices) {
        std::vector<int> axes;
        std::vector<bool> buttons;
        if (d.pad) {
            for (const auto& a : shim::kPadAxes) axes.push_back(sdl.SDL_GetGamepadAxis(d.pad, a.sdl));
            for (const auto& p : shim::kPadButtons) buttons.push_back(sdl.SDL_GetGamepadButton(d.pad, p.sdl));
        } else {
            for (int a = 0; a < sdl.SDL_GetNumJoystickAxes(d.joy); ++a) axes.push_back(sdl.SDL_GetJoystickAxis(d.joy, a));
            for (int k = 0; k < sdl.SDL_GetNumJoystickButtons(d.joy); ++k) buttons.push_back(sdl.SDL_GetJoystickButton(d.joy, k));
        }
        app.detect.axes.push_back(axes);
        app.detect.buttons.push_back(buttons);
    }
    SetWindowTextW(b, L"Press...");
    set_status(kind == Detect::Pad ? L"Press a button or move a stick or trigger on the pad."
               : kind == Detect::WheelAxis ? L"Turn the wheel or press the pedal all the way."
                                           : L"Press the wheel's button.");
}

void detect_poll() {
    Detect& dt = app.detect;
    if (dt.kind == Detect::None) return;
    if (GetTickCount64() - dt.started > 10000) return detect_stop(L"Nothing pressed; detection stopped.");
    if (dt.axes.size() != devices.size()) return detect_stop(L"A device came or went; try again.");
    for (std::size_t n = 0; n < devices.size(); ++n) {
        const Device& d = devices[n];
        if (dt.kind == Detect::Pad && d.pad) {
            const bool analog = dt.row < 3;
            for (int i = 0; i < shim::kNumPadButtons; ++i) {
                if (dt.row == 0) break;  // steering takes an axis
                if (sdl.SDL_GetGamepadButton(d.pad, shim::kPadButtons[i].sdl) && !dt.buttons[n][i]) {
                    SendMessageW(app.pad_combo[dt.row], CB_SETCURSEL, 1 + i, 0);
                    return detect_stop(L"Set. Save or Play to keep it.");
                }
            }
            for (int i = 0; i < shim::kNumPadAxes; ++i) {
                const int v = sdl.SDL_GetGamepadAxis(d.pad, shim::kPadAxes[i].sdl);
                const bool trigger = shim::kPadAxes[i].sdl == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                                     shim::kPadAxes[i].sdl == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                if (dt.row == 0 && trigger) continue;
                if (!analog && !trigger) continue;  // a button action takes a trigger at most
                if (std::abs(v - dt.axes[n][i]) > 20000) {
                    SendMessageW(app.pad_combo[dt.row], CB_SETCURSEL, 1 + shim::kNumPadButtons + i, 0);
                    return detect_stop(L"Set. Save or Play to keep it.");
                }
            }
        } else if (dt.kind == Detect::WheelButton && !d.pad) {
            for (std::size_t k = 0; k < dt.buttons[n].size(); ++k) {
                if (sdl.SDL_GetJoystickButton(d.joy, static_cast<int>(k)) && !dt.buttons[n][k]) {
                    SetWindowTextW(app.button_edit[dt.row], std::to_wstring(k).c_str());
                    return detect_stop(L"Set. Save or Play to keep it.");
                }
            }
        } else if (dt.kind == Detect::WheelAxis && !d.pad) {
            for (std::size_t a = 0; a < dt.axes[n].size(); ++a) {
                if (std::abs(sdl.SDL_GetJoystickAxis(d.joy, static_cast<int>(a)) - dt.axes[n][a]) > 16000) {
                    SetWindowTextW(app.axis_edit[dt.row], std::to_wstring(a).c_str());
                    return detect_stop(L"Set. Save or Play to keep it.");
                }
            }
        }
    }
}

// What the shim would read from the first wheel with these settings.
void update_live() {
    if (GetTickCount64() - app.live_at < 100) return;
    app.live_at = GetTickCount64();
    std::wstring pads, wheel = L"No wheel connected.";
    if (!sdl.ok) {
        pads = L"SDL3.dll not found beside the launcher: detection is off.";
    } else {
        pads = L"Pads: ";
        bool any = false;
        for (const Device& d : devices) {
            if (!d.pad) continue;
            pads += (any ? L", " : L"") + widen(d.name.c_str());
            any = true;
        }
        if (!any) pads += L"none connected";
        for (const Device& d : devices) {
            if (d.pad || !d.wheel) continue;
            const Config c = config_from_page();
            const int axes = sdl.SDL_GetNumJoystickAxes(d.joy);
            const auto raw = [&](int a) { return a >= 0 && a < axes ? sdl.SDL_GetJoystickAxis(d.joy, a) : 0; };
            const auto rest_high = [&](int a) {
                if (c.pedal_rest != 0) return c.pedal_rest > 0;
                Sint16 v = 0;
                if (a >= 0 && a < axes && sdl.SDL_GetJoystickAxisInitialState(d.joy, a, &v) && std::abs(v) > 16000) return v > 0;
                return true;
            };
            const auto pedal = [&](int a) {
                const int v = raw(a);
                const float t = rest_high(a) ? (32767.0f - v) / 65535.0f : (v + 32768.0f) / 65535.0f;
                return std::clamp(t, 0.0f, 1.0f);
            };
            const float steer = std::clamp(raw(c.steering_axis) / 32767.0f * c.steering_scale, -1.0f, 1.0f);
            wchar_t buf[256];
            std::swprintf(buf, 256, L"%hs\nsteering %+.2f   gas %.2f   brake %.2f", d.name.c_str(), steer,
                          pedal(c.gas_axis), pedal(c.brake_axis));
            wheel = buf;
            break;
        }
    }
    SetWindowTextW(app.pad_devices, pads.c_str());
    SetWindowTextW(app.wheel_live, wheel.c_str());
}

void on_timer() {
    if (sdl.ok) {
        SDL_Event e;
        while (sdl.SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_JOYSTICK_ADDED) device_added(e.jdevice.which, narrow(text_of(app.wheel_name)));
            else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) device_removed(e.jdevice.which);
        }
    }
    detect_poll();
    update_live();
}

void on_command(HWND from, int code) {
    if (code != BN_CLICKED) return;
    const std::wstring t = text_of(from);
    if (t == L"Save") save();
    else if (t == L"Play") play();
    else if (t == L"Default layout") {
        const Config d;
        for (int i = 0; i < shim::kNumActions; ++i) SendMessageW(app.pad_combo[i], CB_SETCURSEL, pad_combo_index(d.pad[i]), 0);
    } else if (t == L"Defaults (Logitech G29 / G923)") {
        const Config d;
        for (int i = 0; i < 12; ++i) SetWindowTextW(app.button_edit[i], std::to_wstring(d.buttons[i]).c_str());
        SetWindowTextW(app.axis_edit[0], std::to_wstring(d.steering_axis).c_str());
        SetWindowTextW(app.axis_edit[1], std::to_wstring(d.gas_axis).c_str());
        SetWindowTextW(app.axis_edit[2], std::to_wstring(d.brake_axis).c_str());
    }
    for (int i = 0; i < shim::kNumActions; ++i)
        if (from == app.pad_detect[i]) detect_start(Detect::Pad, i, from);
    for (int i = 0; i < 12; ++i)
        if (from == app.button_detect[i]) detect_start(Detect::WheelButton, i, from);
    for (int i = 0; i < 3; ++i)
        if (from == app.axis_detect[i]) detect_start(Detect::WheelAxis, i, from);
}

LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
            on_command(reinterpret_cast<HWND>(lp), HIWORD(wp));
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lp)->hwndFrom == app.tab && reinterpret_cast<NMHDR*>(lp)->code == TCN_SELCHANGE)
                show_page(static_cast<int>(SendMessageW(app.tab, TCM_GETCURSEL, 0, 0)));
            return 0;
        case WM_HSCROLL:
            update_percent_labels();
            return 0;
        case WM_TIMER:
            on_timer();
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdline, int show) {
    app.dir = exe_dir();
    app.game_exe = app.dir + L"\\Daytona.exe";
    app.edition = edition_of(app.game_exe);
    // The 2017 folder's Daytona.exe is TeknoParrot's patched copy, with the
    // same timestamp; the shim restores it in memory.
    app.shelldata = full_path(app.dir + L"\\..\\ShellData\\ShellData.ini");
    app.gamesettings = full_path(app.dir + L"\\..\\ShellData\\GameSettings.ini");
    app.cfg = shim::load_config(shim::config_path());
    if (cmdline != nullptr && wcsstr(cmdline, L"--play") != nullptr) play();

    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(nullptr);
    app.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    NONCLIENTMETRICSW ncm{sizeof ncm};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    app.font = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"DaytonaLauncher";
    RegisterClassW(&wc);
    RECT r{0, 0, S(780), S(540)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRect(&r, style, FALSE);
    app.wnd = CreateWindowExW(0, wc.lpszClassName, L"Daytona Championship USA", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);

    // Only the tab strip: the pages sit on the window's own background.
    app.tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, S(8), S(8), S(764),
                              S(28), app.wnd, nullptr, inst, nullptr);
    SendMessageW(app.tab, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    const wchar_t* const tabs[NumPages] = {L"Game", L"Rules", L"Controller", L"Wheel"};
    for (int i = 0; i < NumPages; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(tabs[i]);
        SendMessageW(app.tab, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }

    build_game_page();
    build_rules_page();
    build_pad_page();
    build_wheel_page();
    // Above the tab control in the z-order, so they draw over it.
    for (auto& p : app.page)
        for (HWND h : p) SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    const auto bottom = [&](const wchar_t* text, int x, int w) {
        HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, S(x), S(492),
                                 S(w), S(32), app.wnd, nullptr, inst, nullptr);
        SendMessageW(b, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
        return b;
    };
    bottom(L"Save", 560, 90);
    HWND play_button = bottom(L"Play", 660, 110);
    app.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, S(12), S(500), S(530), S(24), app.wnd,
                                 nullptr, inst, nullptr);
    SendMessageW(app.status, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
    if (!exists(app.game_exe)) {
        set_status(L"Put DaytonaLauncher.exe in the folder that holds Daytona.exe.");
        EnableWindow(play_button, FALSE);
    } else if (!exists(app.shelldata)) {
        set_status(L"ShellData.ini was not found next to the game's folder.");
    }

    update_percent_labels();
    int first = PageGame;
    if (cmdline != nullptr) {
        const wchar_t* const names[NumPages] = {L"--page=game", L"--page=rules", L"--page=controller", L"--page=wheel"};
        for (int i = 0; i < NumPages; ++i)
            if (wcsstr(cmdline, names[i]) != nullptr) first = i;
    }
    SendMessageW(app.tab, TCM_SETCURSEL, first, 0);
    show_page(first);
    sdl.start(app.dir);
    SetTimer(app.wnd, 1, 16, nullptr);
    ShowWindow(app.wnd, show);
    SetFocus(play_button);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(app.wnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
