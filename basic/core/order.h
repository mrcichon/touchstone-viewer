#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace core {

// For each of n samples: 1 if the values of the curves rise with `number`, -1 if they fall, 0 otherwise.
// The rank of a curve by value can differ from its rank by number by `allowed`. Equal numbers share ranks, as do equal values.
std::vector<int8_t> ordered_samples(const std::vector<const double*>& y, const std::vector<double>& number, size_t n, size_t allowed);

struct Ordered {
    std::vector<int8_t> direction;
    std::vector<size_t> left_out;  // indices into y
};
// Greedy: leaves out, one at a time, the curve whose removal orders the most samples. Time: leave_out * curves^2 * n.
Ordered ordered_samples_without_some(const std::vector<const double*>& y, const std::vector<double>& number, size_t n, size_t allowed, size_t leave_out);

}  // namespace core
