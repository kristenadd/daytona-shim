#pragma once
// DYT packets, the game <-> shell link (TCP, the game connects to
// 127.0.0.1:18888): 'D' 'Y' 'T', id (2 bytes, high byte first), payload
// length (2, high byte first), four zero bytes, payload.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace shim {

constexpr std::size_t kDytHeader = 11;

// Message ids: the name table at 2019 0x1543830, index plus 20.
enum : std::uint16_t {
    kMsgSecurityChallenge = 0x2A,   // game -> shell, 4 bytes
    kMsgSetSku = 0x2C,              // shell -> game, the greeting (sets the link's ready byte)
    kMsgSetCredit = 0x2D,           // shell -> game, 1 byte: the credit count
    kMsgSecurityAuthenticate = 0x2E,  // shell -> game, 4 bytes: the answer
};

struct DytPacket {
    std::uint16_t id = 0;
    std::vector<std::uint8_t> payload;
};

std::vector<std::uint8_t> dyt_encode(std::uint16_t id, const std::uint8_t* payload, std::uint16_t len);

// Collects bytes from the stream and hands out whole packets. Bytes before a
// "DYT" magic are skipped (counted in `skipped`).
class DytReader {
public:
    void feed(const std::uint8_t* data, std::size_t n);
    std::optional<DytPacket> next();
    std::size_t skipped = 0;

private:
    std::vector<std::uint8_t> buf_;
};

// What the shell sends back for one packet from the game. `greeted` is the
// link's state: the first packet the game sends is answered with the greeting.
std::vector<std::vector<std::uint8_t>> shell_reply(const DytPacket& in, bool& greeted);

}  // namespace shim
