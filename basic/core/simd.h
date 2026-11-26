#pragma once
#include <cstddef>

namespace core {

constexpr double power_floor = 1e-40;  // limit of the power before the logarithm: zero gives -400 dB, not -inf

// out[i] = 10 log10(re[i]^2 + im[i]^2), with neon, avx2 or scalar, selected at first use.
void power_db(const double* re, const double* im, double* out, size_t n);
void phase_deg(const double* re, const double* im, double* out, size_t n);
const char* simd_set_name();

namespace scalar {
void power_db(const double* re, const double* im, double* out, size_t n);
}
namespace neon {
void power_db(const double* re, const double* im, double* out, size_t n);
}
namespace avx2 {
void power_db(const double* re, const double* im, double* out, size_t n);
}

}  // namespace core
