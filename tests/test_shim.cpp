// Host-side tests: the challenge answer and DYT framing.
#include <cstdio>
#include <cstdlib>

#include "diff_runs.hpp"
#include "dyt.hpp"
#include "shell_answer.hpp"

static int failures = 0;
#define CHECK(x)                                                         \
    do {                                                                 \
        if (!(x)) {                                                      \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #x); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

int main() {
    using namespace shim;
    // The check of the answer gives the challenge back.
    std::uint32_t seed = 12345;
    for (int n = 0; n < 2000; ++n) {
        seed = seed * 1664525u + 1013904223u;
        const std::array<std::uint8_t, 4> challenge = {std::uint8_t(seed), std::uint8_t(seed >> 8),
                                                       std::uint8_t(seed >> 16), std::uint8_t(seed >> 24)};
        CHECK(shell_check_transform(shell_answer(challenge)) == challenge);
    }
    CHECK(shell_answer({0, 0, 0, 0}) != shell_answer({1, 0, 0, 0}));

    // Framing.
    const std::uint8_t ch[4] = {1, 2, 3, 4};
    std::vector<std::uint8_t> stream = {0xEE, 'D'};  // junk, then a split magic
    const auto hello = dyt_encode(0x10, nullptr, 0);
    const auto chal = dyt_encode(kMsgSecurityChallenge, ch, 4);
    CHECK(hello.size() == kDytHeader);
    CHECK(chal.size() == kDytHeader + 4 && chal[3] == 0 && chal[4] == 0x2A && chal[6] == 4);
    stream.insert(stream.end(), hello.begin(), hello.end());
    stream.insert(stream.end(), chal.begin(), chal.end());
    DytReader reader;
    // Byte by byte, so every split is seen.
    std::vector<DytPacket> got;
    for (std::uint8_t b : stream) {
        reader.feed(&b, 1);
        while (auto p = reader.next()) got.push_back(*p);
    }
    CHECK(got.size() == 2);
    CHECK(reader.skipped == 2);
    CHECK(got.size() == 2 && got[0].id == 0x10 && got[1].id == kMsgSecurityChallenge && got[1].payload.size() == 4);

    // Replies: the greeting once, then the answer.
    bool greeted = false;
    auto r0 = shell_reply(got[0], greeted);
    CHECK(greeted && r0.size() == 1 && r0[0][4] == kMsgSetSku);
    auto r1 = shell_reply(got[1], greeted);
    CHECK(r1.size() == 1 && r1[0][4] == kMsgSecurityAuthenticate && r1[0][6] == 4);
    if (r1.size() == 1) {
        const std::array<std::uint8_t, 4> answer = {r1[0][11], r1[0][12], r1[0][13], r1[0][14]};
        CHECK(shell_check_transform(answer) == (std::array<std::uint8_t, 4>{1, 2, 3, 4}));
    }

    // Differing runs (the 2017 restore).
    {
        const std::uint8_t a[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const std::uint8_t b[] = {1, 9, 9, 4, 5, 6, 0, 0};
        const auto runs = diff_runs(a, b, sizeof a);
        CHECK(runs.size() == 2 && runs[0].offset == 1 && runs[0].length == 2 && runs[1].offset == 6 &&
              runs[1].length == 2);
        CHECK(diff_runs(a, a, sizeof a).empty());
    }

    if (failures == 0) std::printf("test_shim: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
