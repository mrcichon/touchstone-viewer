#include "time_domain.h"

#include "simd.h"

#include <cmath>
#include <numbers>

namespace core {
namespace {

using std::numbers::pi;

double frequency_step(const double* freq_hz, size_t n) {
    return n > 1 ? (freq_hz[n - 1] - freq_hz[0]) / static_cast<double>(n - 1) : 1.0;
}

// Index of the element of numpy.linspace(first, last, n) nearest to value; the lower index if two are equally near.
// The elements are calculated as numpy does, i * step + first. For a 1 GHz band with 1001 points they are at half-integer
// nanoseconds: a gate edge at an integer nanosecond is midway between two, and their rounding error selects the index.
size_t nearest_linspace_index(double first, double last, size_t n, double value) {
    double step = (last - first) / static_cast<double>(n - 1);
    size_t best = 0;
    double best_distance = std::abs(first - value);
    for (size_t i = 1; i < n; ++i) {
        double t = i + 1 == n ? last : static_cast<double>(i) * step + first;
        double distance = std::abs(t - value);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

// Index of the measured sample at position j of the extended spectrum; -1 where the extension is zero.
long long continued_index(long long j, size_t n, GateEdge edge) {
    long long count = static_cast<long long>(n);
    if (j >= 0 && j < count) return j;
    switch (edge) {
    case GateEdge::zero: return -1;
    case GateEdge::nearest: return j < 0 ? 0 : count - 1;
    case GateEdge::wrap: return ((j % count) + count) % count;
    case GateEdge::reflect: {
        long long period = 2 * count;  // the sequence a b c d d c b a repeats
        long long r = ((j % period) + period) % period;
        return r < count ? r : period - 1 - r;
    }
    }
    return -1;
}

// Properties of kernel_convolution:
// 1. Span. The two gate edges are found on the axis linspace(-0.5/step, 0.5/step, n).
//    Only the number of samples between the edges is used.
//    The spacing of this axis is 1/((n-1) step). One sample of the transform is 1/(n step).
//    Thus the gate length is requested_span * (n-1)/n, rounded to an integer number of samples.
//    For a 1 GHz band one sample is 1 ns.
// 2. Shape. The kernel is the magnitude of the transform W of the gate window.
//    The magnitude removes the position of the gate and makes the negative sidelobes of W positive.
//    The impulse response is thus multiplied by the inverse transform of |W|.
//    This is a real, even function with its centre at t = 0.
//    It is equal to the window only where W has no negative sidelobes.
//    For kaiser with beta 6 (sidelobes approximately 44 dB down) the difference is small.
//    For boxcar the difference is large.
// 3. Position. The spectrum is multiplied by exp(+i 2 pi f center) before the convolution.
//    This moves the response at `center` to t = 0. After the convolution the conjugate is applied.
//    Thus the centre is exact. Only the span is quantised.
// 4. Level. The sum of the kernel is 1. A response fully inside the gate keeps its level.
// 5. Band edges. The convolution reads the spectrum outside the measured band.
//    GateEdge selects the values assumed there. The kernel has the length of the band.
//    At the first and last frequency, half of the values read are assumed values.
//    With an edge mode other than `wrap`, the result is not an exact multiplication in time.
void plan_kernel_convolution(GatePlan& plan, const double* freq_hz) {
    const size_t n = plan.n;
    const GateSpec& spec = plan.spec;
    double step = frequency_step(freq_hz, n);
    double center_s = spec.center_ns * 1e-9, span_s = spec.span_ns * 1e-9;

    size_t start = nearest_linspace_index(-0.5 / step, 0.5 / step, n, center_s - 0.5 * span_s);
    size_t stop = nearest_linspace_index(-0.5 / step, 0.5 / step, n, center_s + 0.5 * span_s);
    size_t width = stop > start ? stop - start : 0;
    std::vector<double> window = make_window(spec.window, width);

    // gate = zeros(start), window, zeros(n - stop); kernel = |ifftshift(fft(fftshift(gate)))| / sum
    std::vector<complex> gate(n, complex(0.0, 0.0));
    for (size_t k = 0; k < width; ++k) gate[start + k] = window[k];
    fftshift(gate.data(), n);
    Fourier(n).forward(gate.data());
    ifftshift(gate.data(), n);
    double sum = 0.0;
    for (size_t k = 0; k < n; ++k) sum += std::abs(gate[k]);
    // An empty gate has a zero kernel. The result is zeros.
    double scale = sum > 0.0 ? 1.0 / sum : 0.0;

    // The convolution is calculated with a transform (refer to apply_kernel_convolution).
    // It uses the spectrum of the kernel at the work length.
    // The taps are stored in reverse order. The convolution is then a correlation.
    size_t length = 1;
    while (length < 2 * n - 1) length <<= 1;
    plan.convolution = Fourier(length);
    plan.kernel_spectrum.assign(length, complex(0.0, 0.0));
    for (size_t k = 0; k < n; ++k) plan.kernel_spectrum[k] = std::abs(gate[n - 1 - k]) * scale;
    plan.convolution.forward(plan.kernel_spectrum.data());
    for (complex& v : plan.kernel_spectrum) v = std::conj(v);

    plan.rotate_re.resize(n);
    plan.rotate_im.resize(n);
    for (size_t i = 0; i < n; ++i) {
        double angle = 2.0 * pi * freq_hz[i] * center_s;
        plan.rotate_re[i] = std::cos(angle);
        plan.rotate_im[i] = std::sin(angle);
    }
}

void apply_kernel_convolution(const GatePlan& plan, const double* re, const double* im, double* out_re, double* out_im) {
    const size_t n = plan.n;
    const size_t length = plan.convolution.size();
    thread_local std::vector<complex> rotated, work;
    rotated.resize(n);
    for (size_t i = 0; i < n; ++i) rotated[i] = multiply(complex(re[i], im[i]), complex(plan.rotate_re[i], plan.rotate_im[i]));

    // Result: out[i] = sum_k taps[k] * x[i + k - before] for i < n.
    // taps is the reversed kernel. x is the spectrum extended outside the band.
    // `before` is the kernel origin of scipy.ndimage.convolve1d: n/2 for odd n, n/2 - 1 for even n.
    // The extended spectrum (2n - 1 samples) is written to `work` one time.
    // The sum is a correlation: inverse(forward(work) * conj(forward(taps))).
    // The work length is 2n - 1 or more, thus the first n outputs have no wrap-around.
    // Cost: n log n. A direct sum costs n^2 and gives the same result to rounding error.
    const long long before = static_cast<long long>(n % 2 == 0 ? n / 2 - 1 : n / 2);
    work.assign(length, complex(0.0, 0.0));
    for (size_t j = 0; j < 2 * n - 1; ++j) {
        long long source = continued_index(static_cast<long long>(j) - before, n, plan.spec.edge);
        if (source >= 0) work[j] = rotated[static_cast<size_t>(source)];
    }
    plan.convolution.forward(work.data());
    for (size_t k = 0; k < length; ++k) work[k] = multiply(work[k], plan.kernel_spectrum[k]);
    plan.convolution.inverse(work.data());

    for (size_t i = 0; i < n; ++i) {  // rotate back: multiply by exp(-i 2 pi f center)
        complex v = multiply(work[i], complex(plan.rotate_re[i], -plan.rotate_im[i]));
        out_re[i] = v.real();
        out_im[i] = v.imag();
    }
}

// time_multiply. The gate window is calculated as a continuous function at each point of the time axis.
// Thus centre and span change the result continuously, and the window is symmetric about the centre.
// The impulse response has period 1/step. A gate that extends past an end of the axis stops there.
// It does not continue at the other end.
void plan_time_multiply(GatePlan& plan, const double* freq_hz) {
    const size_t n = plan.n;
    const GateSpec& spec = plan.spec;
    std::vector<double> t_ns = time_axis_ns(n, frequency_step(freq_hz, n));
    double start = spec.center_ns - 0.5 * spec.span_ns;
    plan.fourier = Fourier(n);
    plan.gate.resize(n);
    for (size_t k = 0; k < n; ++k)
        plan.gate[k] = spec.span_ns > 0.0 ? window_at(spec.window, (t_ns[k] - start) / spec.span_ns) : 0.0;
}

void apply_time_multiply(const GatePlan& plan, const double* re, const double* im, double* out_re, double* out_im) {
    const size_t n = plan.n;
    thread_local std::vector<complex> x;
    x.resize(n);
    for (size_t i = 0; i < n; ++i) x[i] = complex(re[i], im[i]);
    ifftshift(x.data(), n);
    plan.fourier.inverse(x.data());
    fftshift(x.data(), n);  // x is now the impulse response on time_axis_ns
    for (size_t k = 0; k < n; ++k) x[k] *= plan.gate[k];
    ifftshift(x.data(), n);
    plan.fourier.forward(x.data());
    fftshift(x.data(), n);
    for (size_t i = 0; i < n; ++i) {
        out_re[i] = x[i].real();
        out_im[i] = x[i].imag();
    }
}

}  // namespace

std::vector<double> time_axis_ns(size_t n, double step_hz) {
    std::vector<double> t(n);
    double dt_ns = 1e9 / (static_cast<double>(n) * step_hz);
    for (size_t k = 0; k < n; ++k) t[k] = (static_cast<double>(k) - static_cast<double>(n / 2)) * dt_ns;
    return t;
}

TimeResponsePlan make_time_response_plan(const TimeResponseSpec& spec, const double* freq_hz, size_t n) {
    TimeResponsePlan plan;
    plan.n = n;
    size_t pad = spec.pad > 1 ? static_cast<size_t>(spec.pad) : 1;
    plan.fourier = Fourier(n * pad);
    // Padding gives a band that is pad times wider with the same step.
    // The time step decreases by the factor pad. The range 1/step does not change.
    plan.t_ns = time_axis_ns(n * pad, frequency_step(freq_hz, n));

    plan.weight = make_window(spec.window, n);
    double mean = 0.0;
    for (double w : plan.weight) mean += w;
    mean = n ? mean / static_cast<double>(n) : 1.0;
    // The inverse transform divides by the padded length. The window decreases the signal.
    // The scale factor corrects both. The level of a peak then does not depend on these settings.
    double scale = mean > 0.0 ? static_cast<double>(pad) / mean : 0.0;
    for (double& w : plan.weight) w *= scale;
    return plan;
}

void time_response_db(const TimeResponsePlan& plan, const double* re, const double* im, double* db) {
    const size_t n = plan.n, padded = plan.t_ns.size();
    thread_local std::vector<complex> x;
    thread_local std::vector<double> response_re, response_im;
    x.assign(padded, complex(0.0, 0.0));
    for (size_t i = 0; i < n; ++i) x[i] = complex(re[i], im[i]) * plan.weight[i];
    // The position of the zeros and the ifftshift change only the phase of the result.
    ifftshift(x.data(), padded);
    plan.fourier.inverse(x.data());
    fftshift(x.data(), padded);

    response_re.resize(padded);
    response_im.resize(padded);
    for (size_t k = 0; k < padded; ++k) {
        response_re[k] = x[k].real();
        response_im[k] = x[k].imag();
    }
    power_db(response_re.data(), response_im.data(), db, padded);
}

GatePlan make_gate_plan(const GateSpec& spec, const double* freq_hz, size_t n) {
    GatePlan plan;
    plan.spec = spec;
    plan.n = n;
    if (n < 2) return plan;
    if (spec.method == GateMethod::kernel_convolution) plan_kernel_convolution(plan, freq_hz);
    else plan_time_multiply(plan, freq_hz);
    return plan;
}

void apply_gate(const GatePlan& plan, const double* re, const double* im, double* out_re, double* out_im) {
    const size_t n = plan.n;
    if (n < 2) {
        for (size_t i = 0; i < n; ++i) {
            out_re[i] = re[i];
            out_im[i] = im[i];
        }
        return;
    }
    if (plan.spec.method == GateMethod::kernel_convolution) apply_kernel_convolution(plan, re, im, out_re, out_im);
    else apply_time_multiply(plan, re, im, out_re, out_im);
    if (plan.spec.mode == GateMode::bandstop) {
        for (size_t i = 0; i < n; ++i) {
            out_re[i] = re[i] - out_re[i];
            out_im[i] = im[i] - out_im[i];
        }
    }
}

}  // namespace core
