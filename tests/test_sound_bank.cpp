// The launcher's SE.csb reader and ADX decoder, on the game's own bank
// (DAYTONA_SE_CSB=<path to rom/sound/csb/SE.csb>; skipped without it).
// The expected lengths are the bank's own (nsmpl); the decoder was checked
// against a dump of the same sounds (correlation 0.9998).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
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
    // The select music beside the bank (../adx/d1a_bgm01_sel.adx): its loop.
    std::string music = path;
    music = music.substr(0, music.rfind("csb/")) + "adx/d1a_bgm01_sel.adx";
    std::ifstream m(music, std::ios::binary);
    const std::vector<unsigned char> adx((std::istreambuf_iterator<char>(m)), std::istreambuf_iterator<char>());
    launcher::Pcm bgm;
    const bool read = launcher::decode_adx_file(adx.data(), adx.size(), bgm);
    const bool loop_ok = read && bgm.loop_start == 7104 && bgm.loop_end == 2005233 && bgm.channels == 2 &&
                         bgm.samples.size() / 2 == 2005233;
    std::printf("d1a_bgm01_sel          %6zu frames, loop %u .. %u %s\n", bgm.channels ? bgm.samples.size() / bgm.channels : 0,
                bgm.loop_start, bgm.loop_end, loop_ok ? "ok" : "FAIL");
    failures += !loop_ok;
    if (loop_ok) {
        // A seamless loop: the step from the last sample into the loop start is
        // no bigger than the ordinary steps around it.
        const auto at = [&](std::size_t frame, int ch) { return static_cast<int>(bgm.samples[frame * 2 + ch]); };
        int seam = 0, typical = 0;
        for (int ch = 0; ch < 2; ++ch) {
            seam = std::max(seam, std::abs(at(bgm.loop_end - 1, ch) - at(bgm.loop_start, ch)));
            for (std::size_t f = bgm.loop_start + 1; f < bgm.loop_start + 4410; ++f)
                typical = std::max(typical, std::abs(at(f, ch) - at(f - 1, ch)));
        }
        std::printf("  loop seam step %d, largest step in the next 0.1 s %d %s\n", seam, typical,
                    seam <= typical ? "ok" : "FAIL");
        failures += seam > typical;
    }
    return failures == 0 ? 0 : 1;
}
