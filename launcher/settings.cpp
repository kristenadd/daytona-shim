#include "settings.hpp"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace launcher {

namespace {

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

}  // namespace

std::string ansi(const std::wstring& w) {
    char buf[MAX_PATH * 2] = {};
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, buf, sizeof buf, nullptr, nullptr);
    return buf;
}

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

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

std::wstring ini_get(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(section, key, def, buf, 256, file.c_str());
    return buf;
}

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

const wchar_t* const kVolumeKeys[3] = {L"MusicVolume", L"SFXVolume", L"VoiceVolume"};

Install Install::find() { return in_dir(exe_dir()); }

Install Install::in_dir(const std::wstring& dir) {
    Install in;
    in.dir = dir;
    in.game_exe = in.dir + L"\\Daytona.exe";
    // The 2017 folder's Daytona.exe is TeknoParrot's patched copy, with the
    // same timestamp; the shim restores it in memory.
    in.edition = edition_of(in.game_exe);
    in.shelldata = full_path(in.dir + L"\\..\\ShellData\\ShellData.ini");
    in.gamesettings = full_path(in.dir + L"\\..\\ShellData\\GameSettings.ini");
    return in;
}

bool Install::has_game() const { return exists(game_exe); }
bool Install::has_shim() const { return exists(dir + L"\\dinput8.dll"); }

const char* Install::name() const {
    switch (edition) {
        case Edition::Nse2019: return "NEW SEASON EDITION";
        case Edition::Dcu2017: return "2017 EDITION";
        default: return "";
    }
}

namespace {

const wchar_t* edition_key(Edition e) { return e == Edition::Dcu2017 ? L"2017" : L"2019"; }

std::wstring launcher_ini() { return exe_dir() + L"\\launcher.ini"; }

// Daytona.exe files below `base`, `depth` folders deep at most.
void search(const std::wstring& base, int depth, int& budget, std::vector<std::wstring>& found) {
    if (depth < 0 || budget <= 0) return;
    --budget;
    if (exists(base + L"\\Daytona.exe")) found.push_back(base);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        search(base + L"\\" + fd.cFileName, depth - 1, budget, found);
    } while (FindNextFileW(h, &fd) && budget > 0);
    FindClose(h);
}

std::wstring parent(const std::wstring& dir) {
    const std::size_t slash = dir.find_last_of(L'\\');
    return slash == std::wstring::npos || slash < 3 ? dir : dir.substr(0, slash);
}

}  // namespace

std::vector<Install> find_editions(const Install& here) {
    std::vector<Install> eds;
    const auto add = [&](const Install& in) {
        if (in.edition == Edition::Unknown) return;
        for (const Install& e : eds)
            if (e.edition == in.edition) return;
        eds.push_back(in);
    };
    add(here);
    for (Edition e : {Edition::Nse2019, Edition::Dcu2017}) {
        // Absolute, or relative to the launcher's folder.
        std::wstring dir = ini_get(launcher_ini(), L"Editions", edition_key(e));
        const bool absolute = dir.size() > 1 && (dir[1] == L':' || dir[0] == L'\\' || dir[0] == L'/');
        if (!dir.empty() && !absolute) dir = exe_dir() + L"\\" + dir;
        if (!dir.empty()) add(Install::in_dir(full_path(dir)));
    }
    // The launcher's own folder first (each edition in a subfolder of it),
    // then the folders around it.
    int budget = 4000;  // folders looked at, in all
    std::vector<std::wstring> below;
    search(exe_dir(), 4, budget, below);
    for (const std::wstring& dir : below) add(Install::in_dir(dir));
    std::wstring base = here.dir;
    for (int up = 0; up < 3 && eds.size() < 2; ++up) {
        base = parent(base);
        std::vector<std::wstring> found;
        search(base, 4, budget, found);
        for (const std::wstring& dir : found) add(Install::in_dir(dir));
    }
    std::sort(eds.begin(), eds.end(), [](const Install& a, const Install& b) { return a.edition > b.edition; });
    return eds;
}

int last_edition(const std::vector<Install>& eds) {
    const std::wstring key = ini_get(launcher_ini(), L"Launcher", L"Edition");
    for (std::size_t i = 0; i < eds.size(); ++i)
        if (key == edition_key(eds[i].edition)) return static_cast<int>(i);
    return 0;
}

void remember_edition(const Install& in) {
    WritePrivateProfileStringW(L"Launcher", L"Edition", edition_key(in.edition), launcher_ini().c_str());
}

void GameOptions::load(const Install& in) {
    freeplay = ini_get(in.shelldata, L"Credit", L"Freeplay", L"1") != L"0";
    has_audio = !ini_get(in.shelldata, L"Audio", L"MusicVolume").empty();
    for (int i = 0; i < 3; ++i) volume[i] = _wtoi(ini_get(in.shelldata, L"Audio", kVolumeKeys[i], L"100").c_str());
    const auto& r = rules();
    rule.assign(r.size(), 0);
    rule_in_file.assign(r.size(), false);
    for (std::size_t i = 0; i < r.size(); ++i) {
        const std::wstring value = ini_get(in.gamesettings, L"GameSettings", r[i].key);
        for (std::size_t k = 0; k < r[i].options.size(); ++k)
            if (value == r[i].options[k]) rule[i] = static_cast<int>(k);
        rule_in_file[i] = !value.empty();
    }
    rule_initial = rule;
}

bool GameOptions::save(const Install& in) {
    if (in.edition == Edition::Unknown) return true;
    bool ok = ini_put(in.shelldata, L"Credit", L"Freeplay", freeplay ? L"1" : L"0");
    if (has_audio)
        for (int i = 0; i < 3; ++i) ok = ini_put(in.shelldata, L"Audio", kVolumeKeys[i], std::to_wstring(volume[i])) && ok;
    const auto& r = rules();
    for (std::size_t i = 0; i < r.size(); ++i) {
        if (!rule_in_file[i] && rule[i] == rule_initial[i]) continue;
        ok = ini_put(in.gamesettings, L"GameSettings", r[i].key, r[i].options[rule[i]]) && ok;
        rule_in_file[i] = true;
    }
    return ok;
}

void play(const Install& in) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + in.game_exe + L"\"";
    if (!CreateProcessW(in.game_exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, in.dir.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"Daytona.exe did not start.", L"Daytona", MB_ICONERROR);
        ExitProcess(1);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    ExitProcess(0);
}

bool running_under_wine() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

}  // namespace launcher
