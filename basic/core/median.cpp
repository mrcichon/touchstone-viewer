#include "median.h"

#include <algorithm>
#include <vector>

namespace core {

double median_in_range(const double* x, const double* y, size_t n, double x_lo, double x_hi, bool& found) {
    size_t first = static_cast<size_t>(std::lower_bound(x, x + n, x_lo) - x);
    size_t end = static_cast<size_t>(std::upper_bound(x, x + n, x_hi) - x);
    found = end > first;
    if (!found) return 0.0;
    std::vector<double> values(y + first, y + end);
    size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double upper = values[middle];
    if (values.size() % 2 == 1) return upper;
    double lower = *std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
    return 0.5 * (lower + upper);
}

}  // namespace core
