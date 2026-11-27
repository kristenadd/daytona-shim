#pragma once
// Runs of bytes that differ between two buffers of the same length.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace shim {
struct DiffRun {
    std::size_t offset;
    std::size_t length;
};
std::vector<DiffRun> diff_runs(const std::uint8_t* a, const std::uint8_t* b, std::size_t n);
}  // namespace shim
