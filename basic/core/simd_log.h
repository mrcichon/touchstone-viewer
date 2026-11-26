#pragma once
#include <numbers>

// ln x in the vector loops. x = m * 2^e with m in [sqrt(1/2), sqrt(2)): ln x = e ln 2 + ln m.
// With s = (m-1)/(m+1) and z = s^2: ln m = 2 s (1 + z/3 + z^2/5 + ...). z <= 0.0295, and 12 terms leave less than 1e-18 relative.
// x must be a positive normal double.
namespace core::log_constants {

using std::numbers::ln2;
using std::numbers::sqrt2;
constexpr double db_per_neper_power = 4.3429448190325175;  // 10 / ln 10
constexpr double odd_reciprocal[12] = {1.0,      1.0 / 3,  1.0 / 5,  1.0 / 7,  1.0 / 9,  1.0 / 11,
                                       1.0 / 13, 1.0 / 15, 1.0 / 17, 1.0 / 19, 1.0 / 21, 1.0 / 23};

}  // namespace core::log_constants
