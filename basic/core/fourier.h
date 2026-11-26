#pragma once
#include <complex>
#include <cstddef>
#include <vector>

namespace core {

using complex = std::complex<double>;

// DFT of one fixed length: radix-2, or Bluestein for other lengths. The tables are made once; a transform only reads them.
// Scaling as numpy.fft: forward none, inverse 1/n.
class Fourier {
public:
    Fourier() = default;
    explicit Fourier(size_t n);
    size_t size() const { return n_; }
    void forward(complex* x) const;
    void inverse(complex* x) const;

private:
    void transform_power_of_two(complex* x, bool inverse) const;
    void forward_any_length(complex* x) const;

    size_t n_ = 0;
    size_t m_ = 0;                         // radix-2 length: n_, or the Bluestein work length
    std::vector<complex> roots_;           // for each stage length 2, 4, ..., m_: exp(-2 pi i k / length), k < length/2
    std::vector<complex> chirp_;           // exp(-i pi k^2 / n_)
    std::vector<complex> chirp_spectrum_;  // transform of the conjugate chirp, length m_
};

void fftshift(complex* x, size_t n);
void ifftshift(complex* x, size_t n);

// operator* of std::complex is a library call (C Annex G rules for infinity and NaN).
inline complex multiply(complex a, complex b) {
    return complex(a.real() * b.real() - a.imag() * b.imag(), a.real() * b.imag() + a.imag() * b.real());
}

}  // namespace core
