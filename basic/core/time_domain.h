#pragma once
#include "fourier.h"
#include "window.h"

#include <vector>

namespace core {

// t[k] = (k - n/2) / (n * step): zero at the centre. The unambiguous range is 1/step.
std::vector<double> time_axis_ns(size_t n, double step_hz);

struct TimeResponseSpec {
    WindowSpec window{WindowKind::boxcar, 6.0, false};  // applied to the band before the inverse transform
    int pad = 1;                                        // zero padding to pad * n points: interpolates, same range
};

struct TimeResponsePlan {
    size_t n = 0;
    std::vector<double> t_ns;    // n * pad points
    std::vector<double> weight;  // window, scaled so that a peak keeps its level for each window and padding
    Fourier fourier;
};

TimeResponsePlan make_time_response_plan(const TimeResponseSpec& spec, const double* freq_hz, size_t n);
void time_response_db(const TimeResponsePlan& plan, const double* re, const double* im, double* db);  // db: plan.t_ns.size() elements

enum class GateMethod {
    kernel_convolution,  // convolution with |transform of the gate window|, refer to time_domain.cpp
    time_multiply,       // inverse transform, multiplication by the gate window, forward transform
};
enum class GateMode { bandpass, bandstop };
// Values assumed outside the band by kernel_convolution, named as in scipy.ndimage:
// reflect = d c b a | a b c d | d c b a, nearest = a a a | a b c d | d d d.
enum class GateEdge { reflect, nearest, zero, wrap };

struct GateSpec {
    double center_ns = 5.0;
    double span_ns = 4.0;
    WindowSpec window{WindowKind::kaiser, 6.0, true};
    GateMethod method = GateMethod::kernel_convolution;
    GateMode mode = GateMode::bandpass;
    GateEdge edge = GateEdge::reflect;
};

struct GatePlan {
    GateSpec spec;
    size_t n = 0;
    // kernel_convolution
    std::vector<double> rotate_re, rotate_im;  // exp(+i 2 pi f center)
    Fourier convolution;                       // power-of-two length >= 2n - 1
    std::vector<complex> kernel_spectrum;      // conjugate transform of the kernel taps
    // time_multiply
    Fourier fourier;           // length n
    std::vector<double> gate;  // gate window on time_axis_ns
};

GatePlan make_gate_plan(const GateSpec& spec, const double* freq_hz, size_t n);
void apply_gate(const GatePlan& plan, const double* re, const double* im, double* out_re, double* out_im);  // out must not overlap the input

}  // namespace core
