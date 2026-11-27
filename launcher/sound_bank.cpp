#include "sound_bank.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace launcher {
namespace {

// The bank's sound elements, by name (Synth/<name>_wav or _aif).
const char* const kNames[] = {"160_NAME_PANEL_SOUND", "decision", "cancel", "002_ACCEL"};

std::uint32_t be32(const std::uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// A CRI @UTF table: its columns' values per row, as strings or data spans.
struct Utf {
    struct Value {
        std::string text;
        const std::uint8_t* data = nullptr;
        std::uint32_t size = 0;
    };
    std::string name;
    std::vector<std::string> columns;
    std::vector<std::vector<Value>> rows;

    bool parse(const std::uint8_t* p, std::size_t avail) {
        if (avail < 32 || std::memcmp(p, "@UTF", 4) != 0) return false;
        const std::uint32_t size = be32(p + 4) + 8;
        if (size > avail) return false;
        const std::uint8_t* rows_at = p + 8 + be16(p + 10);
        const std::uint8_t* strings = p + 8 + be32(p + 12);
        const std::uint8_t* data = p + 8 + be32(p + 16);
        const std::uint8_t* end = p + size;
        const auto str = [&](std::uint32_t o) {
            const char* s = reinterpret_cast<const char*>(strings + o);
            if (reinterpret_cast<const std::uint8_t*>(s) >= end) return std::string();
            return std::string(s, strnlen(s, static_cast<std::size_t>(end - reinterpret_cast<const std::uint8_t*>(s))));
        };
        name = str(be32(p + 20));
        const int ncol = be16(p + 24), width = be16(p + 26);
        const std::uint32_t nrow = be32(p + 28);
        static const int sizes[16] = {1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 4, 8, 0, 0, 0, 0};
        struct Col {
            int storage, type;
            const std::uint8_t* constant;
        };
        std::vector<Col> cols;
        const std::uint8_t* c = p + 32;
        for (int i = 0; i < ncol; ++i) {
            if (c + 5 > end) return false;
            const int flags = c[0];
            columns.push_back(str(be32(c + 1)));
            c += 5;
            Col col{flags & 0xF0, flags & 0x0F, nullptr};
            if (col.storage == 0x30) {
                col.constant = c;
                c += sizes[col.type];
            }
            cols.push_back(col);
        }
        const auto value = [&](const Col& col, const std::uint8_t* at) {
            Value v;
            if (col.type == 0xA) v.text = str(be32(at));
            if (col.type == 0xB) {
                v.data = data + be32(at);
                v.size = be32(at + 4);
                if (v.data + v.size > end) v = Value{};
            }
            return v;
        };
        for (std::uint32_t r = 0; r < nrow; ++r) {
            const std::uint8_t* at = rows_at + static_cast<std::size_t>(r) * width;
            if (at + width > end) return false;
            std::vector<Value> row;
            for (const Col& col : cols) {
                if (col.storage == 0x50) {
                    row.push_back(value(col, at));
                    at += sizes[col.type];
                } else if (col.storage == 0x30) {
                    row.push_back(value(col, col.constant));
                } else {
                    row.push_back({});
                }
            }
            rows.push_back(row);
        }
        return true;
    }

    int column(const char* n) const {
        for (std::size_t i = 0; i < columns.size(); ++i)
            if (columns[i] == n) return static_cast<int>(i);
        return -1;
    }
};

// Standard CRI ADX (types 3, unencrypted): 4-bit ADPCM in 18-byte frames
// of 32 samples per channel, with a fixed two-tap predictor.
bool decode_adx(const std::uint8_t* p, std::size_t size, Pcm& out) {
    if (size < 20 || p[0] != 0x80 || p[1] != 0x00 || p[4] != 3 || p[19] != 0) return false;
    const std::size_t start = be16(p + 2) + 4;
    const int frame = p[5], channels = p[7];
    const std::uint32_t rate = be32(p + 8), total = be32(p + 12);
    const int cutoff = be16(p + 16);
    // Loop points: version 3 headers keep them at 0x18, version 4 at 0x24
    // (after 16 bytes of history), each as flag, start sample, start byte,
    // end sample, end byte.
    const std::size_t loop_at = p[18] == 4 ? 0x24 : 0x18;
    out.loop_start = out.loop_end = 0;
    if ((p[18] == 3 || p[18] == 4) && start >= loop_at + 20 && be32(p + loop_at) != 0) {
        out.loop_start = be32(p + loop_at + 4);
        out.loop_end = be32(p + loop_at + 12);
        if (out.loop_end <= out.loop_start || out.loop_end > total) out.loop_start = out.loop_end = 0;
    }
    if (frame != 18 || channels < 1 || channels > 2 || rate == 0 || start >= size) return false;
    const double a = std::sqrt(2.0) - std::cos(2.0 * 3.14159265358979323846 * cutoff / rate), b = std::sqrt(2.0) - 1.0;
    const double c = (a - std::sqrt((a + b) * (a - b))) / b;
    const int c1 = static_cast<int>(std::floor(c * 8192)), c2 = static_cast<int>(std::floor(c * c * -4096));
    out.channels = channels;
    out.rate = static_cast<int>(rate);
    out.samples.assign(static_cast<std::size_t>(total) * channels, 0);
    int hist[2][2] = {};
    std::size_t at = start;
    for (std::uint32_t done = 0; done < total; done += 32) {
        for (int ch = 0; ch < channels; ++ch, at += 18) {
            if (at + 18 > size) return done > 0;
            const int scale = (be16(p + at) & 0x1FFF) + 1;
            for (int j = 0; j < 32 && done + j < total; ++j) {
                const int byte = p[at + 2 + j / 2];
                int nib = j % 2 == 0 ? byte >> 4 : byte & 15;
                if (nib >= 8) nib -= 16;
                int s = nib * scale + ((c1 * hist[ch][0] + c2 * hist[ch][1]) >> 12);
                s = std::clamp(s, -32768, 32767);
                hist[ch][1] = hist[ch][0];
                hist[ch][0] = s;
                out.samples[(done + j) * channels + ch] = static_cast<std::int16_t>(s);
            }
        }
    }
    return true;
}

}  // namespace

bool decode_adx_file(const std::uint8_t* data, std::size_t size, Pcm& out) { return decode_adx(data, size, out); }

bool decode_bank(const std::uint8_t* bank, std::size_t size, Pcm out[kBankSounds]) {
    Utf top;
    if (!top.parse(bank, size)) return false;
    const int utf_col = top.column("utf"), name_col = top.column("name");
    for (const auto& row : top.rows) {
        if (utf_col < 0 || name_col < 0 || row[name_col].text != "SOUND_ELEMENT" || row[utf_col].data == nullptr) continue;
        Utf se;
        if (!se.parse(row[utf_col].data, row[utf_col].size)) continue;
        const int n = se.column("name"), d = se.column("data");
        for (const auto& e : se.rows) {
            if (n < 0 || d < 0 || e[d].data == nullptr) continue;
            for (int i = 0; i < kBankSounds; ++i) {
                const std::string want = std::string("Synth/") + kNames[i] + "_";
                if (e[n].text.rfind(want, 0) != 0 || !out[i].samples.empty()) continue;
                Utf aax;  // the sound's segments; the first is the whole of a one-shot
                if (!aax.parse(e[d].data, e[d].size)) continue;
                const int ad = aax.column("data");
                if (ad >= 0 && !aax.rows.empty() && aax.rows[0][ad].data != nullptr)
                    decode_adx(aax.rows[0][ad].data, aax.rows[0][ad].size, out[i]);
            }
        }
    }
    return true;
}

}  // namespace launcher
