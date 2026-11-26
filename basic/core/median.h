#pragma once
#include <cstddef>

namespace core {

// x ascending. found = false if no x is in [x_lo, x_hi].
double median_in_range(const double* x, const double* y, size_t n, double x_lo, double x_hi, bool& found);

}  // namespace core
