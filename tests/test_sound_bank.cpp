// The launcher's SE.csb reader and ADX decoder, on the game's own bank
// (DAYTONA_SE_CSB=<path to rom/sound/csb/SE.csb>; skipped without it).
// The expected lengths are the bank's own (nsmpl); the decoder was checked
// against a dump of the same sounds (correlation 0.9998).
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#include "sound_bank.hpp"

int main() {
    const char* path = std::getenv("DAYTONA_SE_CSB");
    if (path == nullptr) {
        std::puts("test_sound_bank: DAYTONA_SE_CSB not set, skipped");
        return 0;
    }
    std::ifstream f(path, std::ios::binary);
    const std::vector<unsigned char> bank((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    launcher::Pcm pcm[launcher::kBankSounds];
    if (!launcher::decode_bank(bank.data(), bank.size(), pcm)) {
        std::puts("test_sound_bank: bank not read");
        return 1;
    }
    const struct {
        const char* name;
        std::size_t frames;
        int channels;
    } want[launcher::kBankSounds] = {{"161_NAME_ENTRY_THREE", 37029, 2}, {"decision", 56607, 2}, {"cancel", 40350, 2},
                                      {"002_ACCEL", 61109, 1}};
    int failures = 0;
    for (int i = 0; i < launcher::kBankSounds; ++i) {
        const launcher::Pcm& p = pcm[i];
        int peak = 0;
        for (short s : p.samples) peak = std::max(peak, std::abs(static_cast<int>(s)));
        const std::size_t frames = p.channels ? p.samples.size() / p.channels : 0;
        const bool ok = frames == want[i].frames && p.channels == want[i].channels && p.rate == 44100 && peak > 4000;
        std::printf("%-22s %6zu frames, %d ch, %d Hz, peak %5d %s\n", want[i].name, frames, p.channels, p.rate, peak,
                    ok ? "ok" : "FAIL");
        failures += !ok;
    }
    return failures == 0 ? 0 : 1;
}
