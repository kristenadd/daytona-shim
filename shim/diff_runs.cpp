#include "diff_runs.hpp"

namespace shim {
std::vector<DiffRun> diff_runs(const std::uint8_t* a, const std::uint8_t* b, std::size_t n) {
    std::vector<DiffRun> runs;
    std::size_t i = 0;
    while (i < n) {
        if (a[i] == b[i]) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < n && a[i] != b[i]) ++i;
        runs.push_back({start, i - start});
    }
    return runs;
}
}  // namespace shim
