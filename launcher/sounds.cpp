#include "sounds.hpp"

#include "sound_bank.hpp"

#include <windows.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace launcher {

struct Sounds::Clip {
    std::vector<std::int16_t> pcm;  // interleaved
    int channels = 0, rate = 0;
    SDL_AudioStream* stream = nullptr;
};

bool Sounds::load(const std::wstring& game_dir) {
    close();
    HANDLE f = CreateFileW((game_dir + L"\\rom\\sound\\csb\\SE.csb").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<std::uint8_t> bank(static_cast<std::size_t>(std::min<LONGLONG>(sz.QuadPart, 64ll << 20)));
    DWORD got = 0;
    ReadFile(f, bank.data(), static_cast<DWORD>(bank.size()), &got, nullptr);
    CloseHandle(f);
    bank.resize(got);

    clips_ = new Clip[static_cast<int>(Sound::Count)];
    Pcm pcm[kBankSounds];
    if (!decode_bank(bank.data(), bank.size(), pcm)) return false;
    for (int i = 0; i < kBankSounds; ++i) {
        clips_[i].pcm = std::move(pcm[i].samples);
        clips_[i].channels = pcm[i].channels;
        clips_[i].rate = pcm[i].rate;
    }
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_ == 0) return false;
    for (int i = 0; i < static_cast<int>(Sound::Count); ++i) {
        Clip& c = clips_[i];
        if (c.pcm.empty()) continue;
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, c.channels, c.rate};
        c.stream = SDL_CreateAudioStream(&spec, nullptr);
        if (c.stream != nullptr) {
            SDL_SetAudioStreamGain(c.stream, 0.8f);
            SDL_BindAudioStream(device_, c.stream);
        }
    }
    return true;
}

void Sounds::play(Sound s) {
    if (clips_ == nullptr) return;
    Clip& c = clips_[static_cast<int>(s)];
    if (c.stream == nullptr) return;
    SDL_ClearAudioStream(c.stream);
    SDL_PutAudioStreamData(c.stream, c.pcm.data(), static_cast<int>(c.pcm.size() * sizeof(std::int16_t)));
}

float Sounds::seconds(Sound s) const {
    if (clips_ == nullptr) return 0;
    const Clip& c = clips_[static_cast<int>(s)];
    return c.pcm.empty() ? 0.0f : static_cast<float>(c.pcm.size() / c.channels) / c.rate;
}

void Sounds::close() {
    if (clips_ != nullptr) {
        for (int i = 0; i < static_cast<int>(Sound::Count); ++i)
            if (clips_[i].stream != nullptr) SDL_DestroyAudioStream(clips_[i].stream);
        delete[] clips_;
        clips_ = nullptr;
    }
    if (device_ != 0) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

}  // namespace launcher
