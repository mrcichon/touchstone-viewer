#include "simd.h"
#include "simd_log.h"

#include <arm_neon.h>

// Two doubles in each register.
namespace core::neon {
namespace {

using namespace log_constants;

inline float64x2_t natural_log(float64x2_t x) {
    uint64x2_t bits = vreinterpretq_u64_f64(x);
    // Exponent field, with bias 1023.
    int64x2_t exponent = vreinterpretq_s64_u64(vshrq_n_u64(bits, 52));
    // Mantissa with the exponent field set to 1023: m is in [1, 2).
    uint64x2_t mantissa = vorrq_u64(vandq_u64(bits, vdupq_n_u64(0x000FFFFFFFFFFFFFull)), vdupq_n_u64(0x3FF0000000000000ull));
    float64x2_t m = vreinterpretq_f64_u64(mantissa);
    // Move m from [1, 2) into [sqrt(1/2), sqrt(2)). This makes |m - 1| small.
    uint64x2_t upper_half = vcgtq_f64(m, vdupq_n_f64(sqrt2));
    m = vbslq_f64(upper_half, vmulq_f64(m, vdupq_n_f64(0.5)), m);
    // The mask is all ones, which is -1 as an integer. Subtract it to add one to the exponent.
    exponent = vsubq_s64(vsubq_s64(exponent, vdupq_n_s64(1023)), vreinterpretq_s64_u64(upper_half));
    float64x2_t e = vcvtq_f64_s64(exponent);

    float64x2_t s = vdivq_f64(vsubq_f64(m, vdupq_n_f64(1.0)), vaddq_f64(m, vdupq_n_f64(1.0)));
    float64x2_t z = vmulq_f64(s, s);
    float64x2_t series = vdupq_n_f64(odd_reciprocal[11]);
    for (int k = 10; k >= 0; --k) series = vfmaq_f64(vdupq_n_f64(odd_reciprocal[k]), series, z);  // Horner
    float64x2_t ln_m = vmulq_f64(vaddq_f64(s, s), series);
    return vfmaq_f64(ln_m, e, vdupq_n_f64(ln2));
}

}  // namespace

void power_db(const double* re, const double* im, double* out, size_t n) {
    size_t i = 0;
    for (; i + 2 <= n; i += 2) {
        float64x2_t r = vld1q_f64(re + i), q = vld1q_f64(im + i);
        float64x2_t power = vfmaq_f64(vmulq_f64(r, r), q, q);
        power = vmaxq_f64(power, vdupq_n_f64(power_floor));
        vst1q_f64(out + i, vmulq_f64(natural_log(power), vdupq_n_f64(db_per_neper_power)));
    }
    scalar::power_db(re + i, im + i, out + i, n - i);
}

}  // namespace core::neon
