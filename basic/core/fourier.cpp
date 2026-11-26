#include "fourier.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace core {
namespace {

using std::numbers::pi;

bool is_power_of_two(size_t n) { return n && !(n & (n - 1)); }

}  // namespace

Fourier::Fourier(size_t n) : n_(n) {
    if (n < 2) return;
    m_ = n;
    if (!is_power_of_two(n)) {
        m_ = 1;
        while (m_ < 2 * n - 1) m_ <<= 1;
    }
    // The roots are stored in stage order. The inner loop reads them in sequence.
    roots_.reserve(m_ - 1);
    for (size_t length = 2; length <= m_; length <<= 1)
        for (size_t k = 0; k < length / 2; ++k) {
            double angle = -2.0 * pi * static_cast<double>(k) / static_cast<double>(length);
            roots_.emplace_back(std::cos(angle), std::sin(angle));
        }
    if (m_ == n) return;

    // Bluestein algorithm for a length that is not a power of two.
    // With jk = (j^2 + k^2 - (k-j)^2) / 2 the transform is a convolution with a chirp:
    //   X[k] = w[k] * sum_j (x[j] w[j]) * conj(w[k-j]),   w[k] = exp(-i pi k^2 / n)
    // The convolution is calculated with power-of-two transforms of length m >= 2n - 1.
    chirp_.resize(n);
    for (size_t k = 0; k < n; ++k) {
        // k^2 is reduced mod 2n. The chirp has period 2n in k^2.
        // A small argument keeps sin and cos accurate for long transforms.
        double angle = -pi * static_cast<double>((k * k) % (2 * n)) / static_cast<double>(n);
        chirp_[k] = complex(std::cos(angle), std::sin(angle));
    }
    chirp_spectrum_.assign(m_, complex(0.0, 0.0));
    chirp_spectrum_[0] = std::conj(chirp_[0]);
    for (size_t k = 1; k < n; ++k) chirp_spectrum_[k] = chirp_spectrum_[m_ - k] = std::conj(chirp_[k]);
    transform_power_of_two(chirp_spectrum_.data(), false);
}

// Iterative radix-2 Cooley-Tukey transform of m_ points.
// The inverse uses the conjugate roots. The caller applies the scale factor.
void Fourier::transform_power_of_two(complex* x, bool inverse) const {
    const size_t m = m_;
    for (size_t i = 1, j = 0; i < m; ++i) {  // bit reversal permutation
        size_t bit = m >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    const complex* root = roots_.data();
    const double sign = inverse ? -1.0 : 1.0;
    for (size_t length = 2; length <= m; length <<= 1) {
        const size_t half = length / 2;
        for (size_t start = 0; start < m; start += length) {
            for (size_t k = 0; k < half; ++k) {
                double wr = root[k].real(), wi = sign * root[k].imag();
                complex a = x[start + k], b = x[start + k + half];
                double tr = b.real() * wr - b.imag() * wi, ti = b.real() * wi + b.imag() * wr;
                x[start + k] = complex(a.real() + tr, a.imag() + ti);
                x[start + k + half] = complex(a.real() - tr, a.imag() - ti);
            }
        }
        root += half;
    }
}

void Fourier::forward_any_length(complex* x) const {
    thread_local std::vector<complex> work;
    work.assign(m_, complex(0.0, 0.0));
    for (size_t k = 0; k < n_; ++k) work[k] = multiply(x[k], chirp_[k]);
    transform_power_of_two(work.data(), false);
    for (size_t k = 0; k < m_; ++k) work[k] = multiply(work[k], chirp_spectrum_[k]);
    transform_power_of_two(work.data(), true);
    const double scale = 1.0 / static_cast<double>(m_);
    for (size_t k = 0; k < n_; ++k) x[k] = multiply(work[k], chirp_[k]) * scale;
}

void Fourier::forward(complex* x) const {
    if (n_ < 2) return;
    if (m_ == n_) transform_power_of_two(x, false);
    else forward_any_length(x);
}

void Fourier::inverse(complex* x) const {
    if (n_ < 2) return;
    const double scale = 1.0 / static_cast<double>(n_);
    if (m_ == n_) {
        transform_power_of_two(x, true);
        for (size_t k = 0; k < n_; ++k) x[k] *= scale;
        return;
    }
    // inverse(x) = conj(forward(conj(x))) / n. The same chirp tables are used.
    for (size_t k = 0; k < n_; ++k) x[k] = std::conj(x[k]);
    forward_any_length(x);
    for (size_t k = 0; k < n_; ++k) x[k] = std::conj(x[k]) * scale;
}

void fftshift(complex* x, size_t n) { std::rotate(x, x + (n - n / 2), x + n); }
void ifftshift(complex* x, size_t n) { std::rotate(x, x + n / 2, x + n); }

}  // namespace core
