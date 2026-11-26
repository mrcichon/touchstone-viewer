#include "window.h"

#include <cmath>
#include <numbers>

namespace core {
namespace {

using std::numbers::pi;

// Modified Bessel function of the first kind, order zero, from the power series
// I0(x) = sum_k ((x/2)^k / k!)^2. All terms are positive, thus there is no cancellation.
double bessel_i0(double x) {
    double half = 0.5 * x;
    double term = 1.0, sum = 1.0;
    for (int k = 1; k < 500; ++k) {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}

}  // namespace

const char* window_name(WindowKind kind) {
    switch (kind) {
    case WindowKind::boxcar: return "boxcar";
    case WindowKind::hann: return "hann";
    case WindowKind::hamming: return "hamming";
    case WindowKind::blackman: return "blackman";
    case WindowKind::kaiser: return "kaiser";
    case WindowKind::tukey: return "tukey";
    }
    return "";
}

double window_at(const WindowSpec& spec, double u) {
    if (u < 0.0 || u > 1.0) return 0.0;
    switch (spec.kind) {
    case WindowKind::boxcar: return 1.0;
    case WindowKind::hann: return 0.5 - 0.5 * std::cos(2.0 * pi * u);
    case WindowKind::hamming: return 0.54 - 0.46 * std::cos(2.0 * pi * u);
    case WindowKind::blackman: return 0.42 - 0.5 * std::cos(2.0 * pi * u) + 0.08 * std::cos(4.0 * pi * u);
    case WindowKind::kaiser: {
        double r = 2.0 * u - 1.0;
        double inside = 1.0 - r * r;
        return bessel_i0(spec.parameter * std::sqrt(inside > 0.0 ? inside : 0.0)) / bessel_i0(spec.parameter);
    }
    case WindowKind::tukey: {
        double alpha = spec.parameter;
        if (alpha <= 0.0) return 1.0;
        if (alpha >= 1.0) return 0.5 - 0.5 * std::cos(2.0 * pi * u);
        double edge = 0.5 * alpha;  // fraction of the window that each cosine taper covers
        if (u < edge) return 0.5 * (1.0 + std::cos(pi * (u / edge - 1.0)));
        if (u > 1.0 - edge) return 0.5 * (1.0 + std::cos(pi * ((u - 1.0) / edge + 1.0)));
        return 1.0;
    }
    }
    return 0.0;
}

std::vector<double> make_window(const WindowSpec& spec, size_t n) {
    std::vector<double> w(n);
    if (n == 0) return w;
    size_t symmetric_length = spec.periodic ? n + 1 : n;
    if (symmetric_length == 1) {
        w[0] = 1.0;
        return w;
    }
    double last = static_cast<double>(symmetric_length - 1);
    if (spec.kind == WindowKind::tukey && spec.parameter > 0.0 && spec.parameter < 1.0) {
        // scipy puts the end of the taper at an integer sample, floor(alpha (M-1) / 2).
        // The result is different from samples of the continuous window. This code gives the scipy result.
        double alpha = spec.parameter;
        size_t width = static_cast<size_t>(std::floor(alpha * last / 2.0));
        for (size_t k = 0; k < n; ++k) {
            double x = static_cast<double>(k);
            if (k <= width) w[k] = 0.5 * (1.0 + std::cos(pi * (-1.0 + 2.0 * x / alpha / last)));
            else if (k < symmetric_length - width - 1) w[k] = 1.0;
            else w[k] = 0.5 * (1.0 + std::cos(pi * (-2.0 / alpha + 1.0 + 2.0 * x / alpha / last)));
        }
        return w;
    }
    for (size_t k = 0; k < n; ++k) w[k] = window_at(spec, static_cast<double>(k) / last);
    return w;
}

}  // namespace core
