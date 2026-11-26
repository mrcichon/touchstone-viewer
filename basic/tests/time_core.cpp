#include "measurement.h"
#include "simd.h"
#include "time_domain.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

template <typename Work>
static double microseconds_per_call(Work work) {
    using clock = std::chrono::steady_clock;
    int calls = 0;
    auto start = clock::now();
    double elapsed = 0.0;
    while (elapsed < 0.3) {
        work();
        ++calls;
        elapsed = std::chrono::duration<double>(clock::now() - start).count();
    }
    return elapsed * 1e6 / calls;
}

int main() {
    using namespace core;
    std::printf("simd set: %s\n", simd_set_name());
    std::printf("%7s %12s %14s %14s %14s %10s\n", "points", "gate plan", "convolve gate", "multiply gate", "time response", "dB");
    for (size_t n : {481u, 1002u, 4001u, 10001u}) {
        std::vector<double> f(n), re(n), im(n), out_re(n), out_im(n);
        for (size_t i = 0; i < n; ++i) {
            f[i] = 2e9 + 1e9 * static_cast<double>(i) / static_cast<double>(n - 1);
            re[i] = std::cos(0.01 * static_cast<double>(i));
            im[i] = std::sin(0.013 * static_cast<double>(i));
        }
        GateSpec convolve, multiply;
        multiply.method = GateMethod::time_multiply;
        GatePlan convolve_plan = make_gate_plan(convolve, f.data(), n), multiply_plan = make_gate_plan(multiply, f.data(), n);
        TimeResponsePlan response_plan = make_time_response_plan(TimeResponseSpec{}, f.data(), n);
        std::vector<double> db(response_plan.t_ns.size());
        std::printf("%7zu %9.1f us %11.1f us %11.1f us %11.1f us %7.2f us\n", n,
                    microseconds_per_call([&] { make_gate_plan(convolve, f.data(), n); }),
                    microseconds_per_call([&] { apply_gate(convolve_plan, re.data(), im.data(), out_re.data(), out_im.data()); }),
                    microseconds_per_call([&] { apply_gate(multiply_plan, re.data(), im.data(), out_re.data(), out_im.data()); }),
                    microseconds_per_call([&] { time_response_db(response_plan, re.data(), im.data(), db.data()); }),
                    microseconds_per_call([&] { power_db(re.data(), im.data(), out_re.data(), n); }));
    }
    LoadResult loaded;
    std::printf("loading one 1002-point two-port file: %.0f us\n",
                microseconds_per_call([&] { loaded = load_measurement(std::string(REFERENCE_DIR) + "/sample.s2p"); }));
}
