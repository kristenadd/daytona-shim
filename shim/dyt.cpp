#include "dyt.hpp"

#include <algorithm>

#include "shell_answer.hpp"

namespace shim {

std::vector<std::uint8_t> dyt_encode(std::uint16_t id, const std::uint8_t* payload, std::uint16_t len) {
    std::vector<std::uint8_t> p = {'D', 'Y', 'T', static_cast<std::uint8_t>(id >> 8), static_cast<std::uint8_t>(id),
                                   static_cast<std::uint8_t>(len >> 8), static_cast<std::uint8_t>(len), 0, 0, 0, 0};
    if (payload != nullptr) p.insert(p.end(), payload, payload + len);
    return p;
}

void DytReader::feed(const std::uint8_t* data, std::size_t n) { buf_.insert(buf_.end(), data, data + n); }

std::optional<DytPacket> DytReader::next() {
    // Resynchronise on the magic.
    std::size_t start = 0;
    while (start + 3 <= buf_.size() && !(buf_[start] == 'D' && buf_[start + 1] == 'Y' && buf_[start + 2] == 'T'))
        ++start;
    if (start + 3 > buf_.size()) {
        // Keep up to two bytes that may begin a magic.
        const std::size_t keep = std::min<std::size_t>(buf_.size(), 2);
        skipped += buf_.size() - keep;
        buf_.erase(buf_.begin(), buf_.end() - static_cast<std::ptrdiff_t>(keep));
        return std::nullopt;
    }
    if (start != 0) {
        skipped += start;
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(start));
    }
    if (buf_.size() < kDytHeader) return std::nullopt;
    const std::size_t len = static_cast<std::size_t>(buf_[5]) << 8 | buf_[6];
    if (buf_.size() < kDytHeader + len) return std::nullopt;
    DytPacket p;
    p.id = static_cast<std::uint16_t>(buf_[3] << 8 | buf_[4]);
    p.payload.assign(buf_.begin() + kDytHeader, buf_.begin() + static_cast<std::ptrdiff_t>(kDytHeader + len));
    buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(kDytHeader + len));
    return p;
}

std::vector<std::vector<std::uint8_t>> shell_reply(const DytPacket& in, bool& greeted) {
    std::vector<std::vector<std::uint8_t>> out;
    if (!greeted) {
        greeted = true;
        out.push_back(dyt_encode(kMsgSetSku, nullptr, 0));
    }
    if (in.id == kMsgSecurityChallenge && in.payload.size() == 4) {
        const std::array<std::uint8_t, 4> challenge = {in.payload[0], in.payload[1], in.payload[2], in.payload[3]};
        const std::array<std::uint8_t, 4> answer = shell_answer(challenge);
        out.push_back(dyt_encode(kMsgSecurityAuthenticate, answer.data(), 4));
    }
    return out;
}

}  // namespace shim
