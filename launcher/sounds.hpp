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
    // The menu music, rom/sound/adx/d1a_bgm01_sel.adx, looped at the points
    // its ADX header gives (sample 7,104 to the end, about 45 s).
    bool load_music(const std::wstring& game_dir);
    // Once a frame: keeps half a second queued and sets the volume (0..1).
    void update_music(float gain);
    // While the game runs: the audio device closed, the sounds kept.
    void suspend();
    void resume();  // the music starts again from the top
    void close();

    struct Clip;
private:
    unsigned device_ = 0;
    Clip* clips_ = nullptr;
    Clip* music_ = nullptr;
    unsigned music_pos_ = 0, loop_start_ = 0, loop_end_ = 0;
    void open_streams();
    void close_streams();
};

}  // namespace launcher
