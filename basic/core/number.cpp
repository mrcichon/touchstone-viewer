#include "number.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace core {

// Fast path for the usual form: [sign] digits [. digits] [e [sign] digits], maximum 19 significant digits.
// The digits give an integer w and a decimal exponent q. The value is w * 10^q.
// If w <= 2^53 and |q| <= 22, w and 10^|q| are exact doubles.
// One multiplication or division then gives the correctly rounded result (Clinger, 1990).
// All other input goes to std::from_chars, which is also correctly rounded.
bool parse_number(const char*& p, const char* end, double& value) {
    static const double power_of_ten[23] = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                            1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const char* s = p;
    bool negative = false;
    if (s < end && (*s == '+' || *s == '-')) negative = *s++ == '-';
    const char* unsigned_start = s;

    uint64_t w = 0;
    int significant = 0, exponent = 0;
    bool any_digit = false, simple = true;
    auto take_digit = [&](char c) {
        any_digit = true;
        if (significant < 19) {
            w = w * 10 + static_cast<uint64_t>(c - '0');
            if (w) ++significant;
            return true;
        }
        simple = false;  // too many digits; std::from_chars reads the number
        return false;
    };
    for (; s < end && *s >= '0' && *s <= '9'; ++s)
        if (!take_digit(*s)) ++exponent;
    if (s < end && *s == '.') {
        for (++s; s < end && *s >= '0' && *s <= '9'; ++s)
            if (take_digit(*s)) --exponent;
    }
    if (any_digit && s + 1 < end && (*s == 'e' || *s == 'E')) {
        const char* e = s + 1;
        bool negative_exponent = false;
        if (*e == '+' || *e == '-') negative_exponent = *e++ == '-';
        if (e < end && *e >= '0' && *e <= '9') {
            int written = 0;
            for (; e < end && *e >= '0' && *e <= '9'; ++e)
                if (written < 10000) written = written * 10 + (*e - '0');
            exponent += negative_exponent ? -written : written;
            s = e;
        }
    }

    if (any_digit && simple && w <= (uint64_t{1} << 53) && exponent >= -22 && exponent <= 22) {
        double magnitude = static_cast<double>(w);
        magnitude = exponent < 0 ? magnitude / power_of_ten[-exponent] : magnitude * power_of_ten[exponent];
        value = negative ? -magnitude : magnitude;
        p = s;
        return true;
    }
    if (!any_digit) return false;  // rejects "inf" and "nan"
    // std::from_chars accepts a minus sign. It does not accept a plus sign.
    const char* start = negative ? unsigned_start - 1 : unsigned_start;
    double parsed;
    auto [after, error] = std::from_chars(start, end, parsed);
    if (error != std::errc() || !std::isfinite(parsed)) return false;
    value = parsed;
    p = after;
    return true;
}

}  // namespace core
