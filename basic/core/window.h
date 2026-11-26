#pragma once
#include <cstddef>
#include <vector>

namespace core {

enum class WindowKind { boxcar, hann, hamming, blackman, kaiser, tukey };

struct WindowSpec {
    WindowKind kind = WindowKind::kaiser;
    double parameter = 6.0;  // kaiser: beta; tukey: tapered fraction
    bool periodic = true;    // scipy fftbins=True: the first n samples of the symmetric window of n+1
};

const char* window_name(WindowKind kind);
std::vector<double> make_window(const WindowSpec& spec, size_t n);  // equal to scipy.signal.get_window
double window_at(const WindowSpec& spec, double u);                 // the window as a function of u in [0, 1]; 0 outside

}  // namespace core
