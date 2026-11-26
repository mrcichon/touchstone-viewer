#include "simd.h"
#include "simd_log.h"

#include <immintrin.h>

// Four doubles in each register, otherwise as simd_neon.cpp.
// Compiled with AVX2 and FMA: call only after the CPU check in simd.cpp, and use no standard library templates here.
// An inline function instantiated here contains AVX2 code, and the linker can select that copy for the whole program.
namespace core::avx2 {
namespace {

using namespace log_constants;

inline __m256d natural_log(__m256d x) {
    __m256i bits = _mm256_castpd_si256(x);
    __m256i biased_exponent = _mm256_srli_epi64(bits, 52);
    // AVX2 has no conversion from 64-bit integer to double. The biased exponent is less than 2^11:
    // put it in the mantissa of 2^52 and subtract 2^52, which is exact.
    const __m256d two_52 = _mm256_set1_pd(4503599627370496.0);
    __m256d e = _mm256_sub_pd(_mm256_castsi256_pd(_mm256_or_si256(biased_exponent, _mm256_castpd_si256(two_52))), two_52);
    e = _mm256_sub_pd(e, _mm256_set1_pd(1023.0));

    __m256i mantissa = _mm256_or_si256(_mm256_and_si256(bits, _mm256_set1_epi64x(0x000FFFFFFFFFFFFFll)),
                                       _mm256_set1_epi64x(0x3FF0000000000000ll));
    __m256d m = _mm256_castsi256_pd(mantissa);  // in [1, 2)
    // Move m into [sqrt(1/2), sqrt(2)). Add one to the exponent where m was halved.
    __m256d upper_half = _mm256_cmp_pd(m, _mm256_set1_pd(sqrt2), _CMP_GT_OQ);
    m = _mm256_blendv_pd(m, _mm256_mul_pd(m, _mm256_set1_pd(0.5)), upper_half);
    e = _mm256_add_pd(e, _mm256_and_pd(upper_half, _mm256_set1_pd(1.0)));

    __m256d one = _mm256_set1_pd(1.0);
    __m256d s = _mm256_div_pd(_mm256_sub_pd(m, one), _mm256_add_pd(m, one));
    __m256d z = _mm256_mul_pd(s, s);
    __m256d series = _mm256_set1_pd(odd_reciprocal[11]);
    for (int k = 10; k >= 0; --k) series = _mm256_fmadd_pd(series, z, _mm256_set1_pd(odd_reciprocal[k]));  // Horner
    __m256d ln_m = _mm256_mul_pd(_mm256_add_pd(s, s), series);
    return _mm256_fmadd_pd(e, _mm256_set1_pd(ln2), ln_m);
}

}  // namespace

void power_db(const double* re, const double* im, double* out, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) {
        __m256d r = _mm256_loadu_pd(re + i), q = _mm256_loadu_pd(im + i);
        __m256d power = _mm256_fmadd_pd(q, q, _mm256_mul_pd(r, r));
        power = _mm256_max_pd(power, _mm256_set1_pd(power_floor));
        _mm256_storeu_pd(out + i, _mm256_mul_pd(natural_log(power), _mm256_set1_pd(db_per_neper_power)));
    }
    scalar::power_db(re + i, im + i, out + i, n - i);
}

}  // namespace core::avx2
