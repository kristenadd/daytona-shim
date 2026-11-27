#pragma once
// What both launcher screens share: the install's paths and edition, the
// game's settings files (ShellData.ini, GameSettings.ini), and starting the
// game.
#include <string>
#include <vector>

namespace launcher {

enum class Edition { Unknown, Dcu2017, Nse2019 };

struct Install {
    std::wstring dir, game_exe, shelldata, gamesettings;
    Edition edition = Edition::Unknown;
    static Install find();  // beside the launcher
    bool has_game() const;
};

std::wstring ini_get(const std::wstring& file, const wchar_t* section, const wchar_t* key, const wchar_t* def = L"");
// The game's own files: a copy is kept as <file>.shim-backup before the
// launcher first changes one.
bool ini_put(const std::wstring& file, const wchar_t* section, const wchar_t* key, const std::wstring& value);
std::wstring widen(const char* s);
std::string narrow(const std::wstring& w);
bool exists(const std::wstring& p);

// The game's rules (GameSettings.ini [GameSettings]): the game's own table
// (2019 0x118A510), its default first.
struct Rule {
    const wchar_t* key;
    std::vector<const wchar_t*> options;
};
const std::vector<Rule>& rules();

// The settings both screens edit.
struct GameOptions {
    bool freeplay = true;
    bool has_audio = false;  // the 2017 file has no [Audio]
    int volume[3] = {80, 100, 90};  // music, effects, voice
    std::vector<int> rule;          // index into rules()[i].options
    std::vector<int> rule_initial;
    std::vector<bool> rule_in_file;

    void load(const Install& in);
    // A rule goes into the file when it is there already or was changed.
    bool save(const Install& in);
};
extern const wchar_t* const kVolumeKeys[3];

// Starts Daytona.exe in its folder and waits for it (so Steam sees one
// session), then ends the launcher.
[[noreturn]] void play(const Install& in);

// The full-screen, controller-driven launcher (Steam Deck): true when running
// under Wine / Proton.
bool running_under_wine();

int run_desktop(const Install& in, const wchar_t* cmdline, int show);
int run_big(const Install& in);

}  // namespace launcher
