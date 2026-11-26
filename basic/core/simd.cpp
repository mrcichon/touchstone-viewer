#include "simd.h"

#include <cmath>
#include <numbers>

namespace core {
namespace scalar {

void power_db(const double* re, const double* im, double* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        double power = re[i] * re[i] + im[i] * im[i];
        out[i] = 10.0 * std::log10(power > power_floor ? power : power_floor);
    }
}

}  // namespace scalar

namespace {

struct SimdSet {
    void (*power_db)(const double*, const double*, double*, size_t);
    const char* name;
};

SimdSet choose_simd_set() {
#if defined(__aarch64__)
    return {neon::power_db, "neon"};
#elif defined(__x86_64__)
    // True only if the operating system also saves the YMM registers.
    if (__builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma")) return {avx2::power_db, "avx2"};
    return {scalar::power_db, "scalar"};
#else
    return {scalar::power_db, "scalar"};
#endif
}

const SimdSet& simd_set() {
    static const SimdSet chosen = choose_simd_set();
    return chosen;
}

}  // namespace

void power_db(const double* re, const double* im, double* out, size_t n) { simd_set().power_db(re, im, out, n); }
const char* simd_set_name() { return simd_set().name; }

void phase_deg(const double* re, const double* im, double* out, size_t n) {
    constexpr double degrees_per_radian = 180.0 / std::numbers::pi;
    for (size_t i = 0; i < n; ++i) out[i] = std::atan2(im[i], re[i]) * degrees_per_radian;
}

}  // namespace core
