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

namespace {

std::vector<std::uint8_t> read_file(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(std::min<LONGLONG>(sz.QuadPart, 64ll << 20)));
    DWORD got = 0;
    ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &got, nullptr);
    CloseHandle(f);
    data.resize(got);
    return data;
}

}  // namespace

bool Sounds::load(const std::wstring& game_dir) {
    close();
    const std::vector<std::uint8_t> bank = read_file(game_dir + L"\\rom\\sound\\csb\\SE.csb");
    if (bank.empty()) return false;
    Pcm pcm[kBankSounds];
    if (!decode_bank(bank.data(), bank.size(), pcm)) return false;
    clips_ = new Clip[static_cast<int>(Sound::Count)];
    for (int i = 0; i < kBankSounds; ++i) {
        clips_[i].pcm = std::move(pcm[i].samples);
        clips_[i].channels = pcm[i].channels;
        clips_[i].rate = pcm[i].rate;
    }
    open_streams();
    return device_ != 0;
}

bool Sounds::load_music(const std::wstring& game_dir) {
    if (music_ != nullptr) return false;
    const std::vector<std::uint8_t> adx = read_file(game_dir + L"\\rom\\sound\\adx\\d1a_bgm01_sel.adx");
    Pcm pcm;
    if (adx.empty() || !decode_adx_file(adx.data(), adx.size(), pcm) || pcm.samples.empty()) return false;
    music_ = new Clip;
    music_->pcm = std::move(pcm.samples);
    music_->channels = pcm.channels;
    music_->rate = pcm.rate;
    const unsigned frames = static_cast<unsigned>(music_->pcm.size() / pcm.channels);
    loop_start_ = pcm.loop_end > pcm.loop_start ? pcm.loop_start : 0;
    loop_end_ = pcm.loop_end > pcm.loop_start ? pcm.loop_end : frames;
    music_pos_ = 0;
    open_streams();
    return music_->stream != nullptr;
}

void Sounds::open_streams() {
    if (device_ == 0) device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (device_ == 0) return;
    const auto open = [&](Clip& c, float gain) {
        if (c.pcm.empty() || c.stream != nullptr) return;
        const SDL_AudioSpec spec{SDL_AUDIO_S16LE, c.channels, c.rate};
        c.stream = SDL_CreateAudioStream(&spec, nullptr);
        if (c.stream == nullptr) return;
        SDL_SetAudioStreamGain(c.stream, gain);
        SDL_BindAudioStream(device_, c.stream);
    };
    if (clips_ != nullptr)
        for (int i = 0; i < static_cast<int>(Sound::Count); ++i) open(clips_[i], 0.8f);
    if (music_ != nullptr) open(*music_, 0.0f);  // its volume comes from update_music
}

void Sounds::close_streams() {
    const auto shut = [](Clip& c) {
        if (c.stream != nullptr) SDL_DestroyAudioStream(c.stream);
        c.stream = nullptr;
    };
    if (clips_ != nullptr)
        for (int i = 0; i < static_cast<int>(Sound::Count); ++i) shut(clips_[i]);
    if (music_ != nullptr) shut(*music_);
    if (device_ != 0) SDL_CloseAudioDevice(device_);
    device_ = 0;
}

void Sounds::suspend() { close_streams(); }

void Sounds::resume() {
    music_pos_ = 0;
    open_streams();
}

void Sounds::play(Sound s) {
    if (clips_ == nullptr) return;
    Clip& c = clips_[static_cast<int>(s)];
    if (c.stream == nullptr) return;
    SDL_ClearAudioStream(c.stream);
    SDL_PutAudioStreamData(c.stream, c.pcm.data(), static_cast<int>(c.pcm.size() * sizeof(std::int16_t)));
}

void Sounds::update_music(float gain) {
    if (music_ == nullptr || music_->stream == nullptr) return;
    SDL_SetAudioStreamGain(music_->stream, gain);
    const int frame_bytes = music_->channels * static_cast<int>(sizeof(std::int16_t));
    while (SDL_GetAudioStreamQueued(music_->stream) < music_->rate / 2 * frame_bytes) {
        const unsigned n = std::min(loop_end_ - music_pos_, 4096u);
        SDL_PutAudioStreamData(music_->stream, music_->pcm.data() + static_cast<std::size_t>(music_pos_) * music_->channels,
                               static_cast<int>(n) * frame_bytes);
        music_pos_ += n;
        if (music_pos_ >= loop_end_) music_pos_ = loop_start_;  // the seamless loop
    }
}

float Sounds::seconds(Sound s) const {
    if (clips_ == nullptr) return 0;
    const Clip& c = clips_[static_cast<int>(s)];
    return c.pcm.empty() ? 0.0f : static_cast<float>(c.pcm.size() / c.channels) / c.rate;
}

void Sounds::close() {
    close_streams();
    delete music_;
    music_ = nullptr;
    delete[] clips_;
    clips_ = nullptr;
}

}  // namespace launcher
