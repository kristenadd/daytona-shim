#pragma once
// The full-screen launcher's sounds, read from the player's own game: the
// CRI sound bank rom/sound/csb/SE.csb (@UTF tables holding ADX), decoded at
// start. Nothing is played when the file or a sound is missing.
#include <string>

namespace launcher {

enum class Sound { Move, Decide, Cancel, Start, Count };

class Sounds {
public:
    bool load(const std::wstring& game_dir);  // the folder holding Daytona.exe
    void play(Sound s);
    float seconds(Sound s) const;  // length, 0 when not loaded
    void close();

    struct Clip;
private:
    unsigned device_ = 0;
    Clip* clips_ = nullptr;
};

}  // namespace launcher
