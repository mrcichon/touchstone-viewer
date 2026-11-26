#include "fourier.h"
#include "measurement.h"
#include "median.h"
#include "names.h"
#include "order.h"
#include "number.h"
#include "paths.h"
#include "simd.h"
#include "time_domain.h"
#include "window.h"

#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

static int failures = 0;
using std::numbers::pi;

static std::vector<double> reference(const std::string& name) {
    std::string path = std::string(REFERENCE_DIR) + "/" + name + ".f64";
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    std::vector<double> v;
    if (!in) {
        std::printf("FAIL missing %s\n", path.c_str());
        ++failures;
        return v;
    }
    v.resize(static_cast<size_t>(in.tellg()) / sizeof(double));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(v.size() * sizeof(double)));
    return v;
}

static void expect_close(const std::string& what, const double* got, const std::vector<double>& want, double tolerance) {
    double worst = 0.0;
    for (size_t i = 0; i < want.size(); ++i) {
        double d = std::abs(got[i] - want[i]);
        if (!(d <= worst)) worst = d;  // also true for NaN
    }
    bool ok = !want.empty() && worst <= tolerance;
    std::printf("%s %-40s worst error %.3g (allowed %.0e)\n", ok ? "ok  " : "FAIL", what.c_str(), worst, tolerance);
    if (!ok) ++failures;
}

static void expect(const std::string& what, bool ok) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

static bool near(double a, double b, double tolerance = 1e-12) { return std::abs(a - b) <= tolerance; }

static const core::Measurement& sample() {
    static core::LoadResult r = core::load_measurement(std::string(REFERENCE_DIR) + "/sample.s2p");
    if (!r.error.empty()) {
        std::printf("FAIL load sample: %s\n", r.error.c_str());
        ++failures;
    }
    return r.measurement;
}

static size_t index_of(const char* name) {  // "s21" -> out port 2, in port 1
    return static_cast<size_t>(sample().parameter(name[1] - '0', name[2] - '0'));
}

static void test_loader_against_reference() {
    const core::Measurement& m = sample();
    expect("loader: two ports, phase present", m.ports == 2 && m.has_phase);
    expect_close("loader: frequency", m.freq_hz.data(), reference("f_hz"), 1e-3);
    for (const char* p : {"s11", "s21", "s12", "s22"}) {
        expect_close(std::string("loader: real ") + p, m.re[index_of(p)].data(), reference(std::string(p) + "_re"), 1e-13);
        expect_close(std::string("loader: imag ") + p, m.im[index_of(p)].data(), reference(std::string(p) + "_im"), 1e-13);
    }
}

static core::LoadResult load_text(const std::string& file_name, const std::string& text) {
    std::filesystem::path folder = std::filesystem::temp_directory_path() / core::path_from_utf8("touchstone viewer test, b\xC5\x82\xC4\x99" "dy");
    std::filesystem::create_directories(folder);
    std::filesystem::path path = folder / file_name;
    std::ofstream(path, std::ios::binary) << text;
    return core::load_measurement(core::utf8_from_path(path));
}

static void test_loader_formats() {
    {
        auto r = load_text("ri.s1p", "! header\n# Hz S RI R 50\n1e9 0.5 -0.25\n2e9 +0.1 0.2 ! trailing comment\n");
        const auto& m = r.measurement;
        expect("loader: real/imaginary, Hz, plus sign, comments",
               r.error.empty() && m.ports == 1 && m.points() == 2 && near(m.freq_hz[1], 2e9) && near(m.re[0][0], 0.5) && near(m.im[0][0], -0.25) && near(m.re[0][1], 0.1));
    }
    {
        auto r = load_text("ma.s1p", "# MHz S MA\n1000 0.5 90\n2000 1 180\n");
        const auto& m = r.measurement;
        expect("loader: magnitude/angle, MHz", r.error.empty() && near(m.freq_hz[0], 1e9) && near(m.re[0][0], 0.0) && near(m.im[0][0], 0.5) && near(m.re[0][1], -1.0));
    }
    {
        auto r = load_text("comma.s1p", "# GHz S DB R 50\n1,5 -6,020599913279624 0\n2,5 0 0\n");
        const auto& m = r.measurement;
        expect("loader: dB/angle with decimal commas", r.error.empty() && near(m.freq_hz[0], 1.5e9) && near(m.re[0][0], 0.5) && near(m.re[0][1], 1.0));
    }
    {
        auto r = load_text("noise.s2p", "# GHz S RI\n1 11 0 21 0 12 0 22 0\n2 11 0 21 0 12 0 22 0\n1 2 0.1 20 0.3\n2 2 0.1 20 0.3\n");
        const auto& m = r.measurement;
        expect("loader: two-port order S11 S21 S12 S22, noise rows dropped",
               r.error.empty() && m.points() == 2 && near(m.re[static_cast<size_t>(m.parameter(2, 1))][0], 21.0) && near(m.re[static_cast<size_t>(m.parameter(1, 2))][0], 12.0));
    }
    {
        auto r = load_text("version2.s2p", "[Version] 2.0\n# GHz S RI\n[Number of Ports] 2\n[Two-Port Data Order] 12_21\n[Reference] 50\n75\n"
                                           "[Network Data]\n1 11 0 12 0 21 0 22 0\n2 11 0 12 0 21 0 22 0\n[End]\n");
        const auto& m = r.measurement;
        expect("loader: version 2 keywords, 12_21 order, reference values not read as data",
               r.error.empty() && m.points() == 2 && near(m.re[static_cast<size_t>(m.parameter(2, 1))][0], 21.0) && near(m.re[static_cast<size_t>(m.parameter(1, 2))][1], 12.0));
    }
    {
        std::string text = "# GHz S RI\n";
        for (int f = 1; f <= 2; ++f) {
            text += std::to_string(f);
            for (int out = 1; out <= 4; ++out) {
                for (int in = 1; in <= 4; ++in) text += " " + std::to_string(out * 10 + in) + " 0";
                text += "\n";
            }
        }
        auto r = load_text("four.s4p", text);
        const auto& m = r.measurement;
        expect("loader: four ports, rows wrapped over lines",
               r.error.empty() && m.ports == 4 && m.points() == 2 && near(m.re[static_cast<size_t>(m.parameter(3, 2))][1], 32.0) && near(m.re[static_cast<size_t>(m.parameter(4, 1))][0], 41.0));
    }
    expect("loader: two-port data in a .s1p file is refused", !load_text("wrong.s1p", "# GHz S RI\n1 11 0 21 0 12 0 22 0\n2 11 0 21 0 12 0 22 0\n").error.empty());
    expect("loader: a single frequency point is refused", !load_text("short.s1p", "# GHz S RI\n1 0.5 0\n").error.empty());
    expect("loader: Y parameters are refused", !load_text("y.s1p", "# GHz Y RI\n1 0.5 0\n2 0.5 0\n").error.empty());
    expect("loader: unknown extension is refused", !load_text("notes.txt", "1 2 3\n").error.empty());
    {
        auto r = load_text("analyzer.csv", "Sweep SA\nStart Frequency,1000\nTrace Data\n1000,-20\n2000,-40\n");
        const auto& m = r.measurement;
        expect("loader: spectrum analyzer csv", r.error.empty() && !m.has_phase && m.ports == 1 && m.points() == 2 && near(m.freq_hz[1], 2000.0) && near(m.re[0][0], 0.1) && near(m.re[0][1], 0.01));
    }
    {
        auto r = load_text("semicolon.csv", "frequency;level\r\n1000,5;-20,0\r\n2000;-40\r\n");
        const auto& m = r.measurement;
        expect("loader: csv with ';' and decimal commas", r.error.empty() && m.points() == 2 && near(m.freq_hz[0], 1000.5) && near(m.re[0][0], 0.1));
        expect("loader: a plain csv is refused when only analyzer exports are wanted", !core::load_measurement(m.path, core::CsvRule::analyzer_export_only).error.empty());
    }
}

static void test_number_parsing() {
    std::mt19937_64 rng(3);
    int wrong = 0, total = 0;
    auto check = [&](const std::string& text) {
        double fast = 0.0, slow = 0.0;
        const char* p = text.data();
        const char* start = *p == '+' ? p + 1 : p;
        bool fast_ok = core::parse_number(p, text.data() + text.size(), fast);
        auto [after, error] = std::from_chars(start, text.data() + text.size(), slow);
        bool slow_ok = error == std::errc();
        ++total;
        if (fast_ok != slow_ok || (fast_ok && (std::memcmp(&fast, &slow, sizeof fast) != 0 || p != after))) {
            if (++wrong < 5) std::printf("     differs: %s\n", text.c_str());
        }
    };
    for (int i = 0; i < 200000; ++i) {
        std::string text;
        if (rng() % 3 == 0) text += rng() % 2 ? '-' : '+';
        int whole = static_cast<int>(rng() % 6), fraction = static_cast<int>(rng() % 17);
        for (int d = 0; d < whole; ++d) text += static_cast<char>('0' + rng() % 10);
        if (fraction || rng() % 2) text += '.';
        for (int d = 0; d < fraction; ++d) text += static_cast<char>('0' + rng() % 10);
        if (whole + fraction == 0) text += '7';
        if (rng() % 3 == 0) text += (rng() % 2 ? "e" : "E") + std::string(rng() % 2 ? "-" : "") + std::to_string(rng() % 40);
        check(text);
    }
    for (const char* text : {"0", "-0", "0.0", ".5", "5.", "1e5", "1E+5", "1e", "1e+", "12abc", "-11.5193163463666", "9007199254740993", "1e23", "1e-23",
                             "123456789012345678901234567890", "0.000000000000000000000000001", "1e400", "4.9e-324"})
        check(text);
    double unused;
    bool refused = true;
    for (const char* text : {"inf", "-inf", "nan", "abc", "-", "+", ".", "e5"}) {
        const char* p = text;
        if (core::parse_number(p, text + std::strlen(text), unused) || p != text) refused = false;
    }
    std::printf("%s number parsing: %d of %d strings differ from from_chars\n", wrong ? "FAIL" : "ok  ", wrong, total);
    if (wrong) ++failures;
    expect("number parsing: inf, nan and non-numbers are refused", refused);
}

static void test_fourier() {
    std::mt19937 rng(1);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    for (size_t n : {1u, 2u, 3u, 8u, 37u, 481u, 1001u, 1002u, 1024u}) {
        std::vector<core::complex> x(n), direct(n);
        for (auto& v : x) v = core::complex(uniform(rng), uniform(rng));
        for (size_t k = 0; k < n; ++k) {  // the definition, O(n^2)
            core::complex sum = 0.0;
            for (size_t j = 0; j < n; ++j) {
                double angle = -2.0 * pi * static_cast<double>((j * k) % n) / static_cast<double>(n);
                sum += x[j] * core::complex(std::cos(angle), std::sin(angle));
            }
            direct[k] = sum;
        }
        core::Fourier fourier(n);
        std::vector<core::complex> y = x;
        fourier.forward(y.data());
        double worst = 0.0;
        for (size_t k = 0; k < n; ++k) worst = std::max(worst, std::abs(y[k] - direct[k]));
        fourier.inverse(y.data());
        double round_trip = 0.0;
        for (size_t k = 0; k < n; ++k) round_trip = std::max(round_trip, std::abs(y[k] - x[k]));
        bool ok = worst < 1e-10 && round_trip < 1e-12;
        std::printf("%s fourier: n = %-27zu vs definition %.3g, round trip %.3g\n", ok ? "ok  " : "FAIL", n, worst, round_trip);
        if (!ok) ++failures;
    }
}

static void test_windows() {
    struct Case { const char* name; core::WindowKind kind; double parameter; };
    const Case cases[] = {{"boxcar", core::WindowKind::boxcar, 0},   {"hann", core::WindowKind::hann, 0},
                          {"hamming", core::WindowKind::hamming, 0}, {"blackman", core::WindowKind::blackman, 0},
                          {"kaiser6", core::WindowKind::kaiser, 6},  {"kaiser12", core::WindowKind::kaiser, 12},
                          {"tukey03", core::WindowKind::tukey, 0.3}};
    for (const Case& c : cases)
        for (size_t n : {37u, 64u})
            for (bool periodic : {true, false}) {
                std::string name = std::string("window_") + c.name + "_" + std::to_string(n) + (periodic ? "_periodic" : "_symmetric");
                std::vector<double> w = core::make_window({c.kind, c.parameter, periodic}, n);
                expect_close(name, w.data(), reference(name), 1e-13);
            }
}

struct SimdSet {
    const char* name;
    void (*power_db)(const double*, const double*, double*, size_t);
};

static void test_simd_set_against_scalar(const SimdSet& set) {
    std::mt19937 rng(2);
    std::uniform_real_distribution<double> uniform(-1.0, 1.0);
    std::uniform_real_distribution<double> exponent(-18.0, 3.0);
    for (size_t n : {0u, 1u, 3u, 7u, 8u, 9u, 17u, 31u, 1002u}) {
        std::vector<double> a(n), b(n), want(n), got(n);
        for (size_t i = 0; i < n; ++i) {
            a[i] = uniform(rng) * std::pow(10.0, exponent(rng));
            b[i] = uniform(rng) * std::pow(10.0, exponent(rng));
        }
        if (n > 2) a[1] = b[1] = 0.0;  // a power of zero must give the floor value, not -inf
        core::scalar::power_db(a.data(), b.data(), want.data(), n);
        set.power_db(a.data(), b.data(), got.data(), n);
        double worst = 0.0;
        for (size_t i = 0; i < n; ++i) worst = std::max(worst, std::abs(got[i] - want[i]));
        bool ok = worst < 1e-12;
        std::printf("%s %s vs scalar: n = %-20zu dB error %.3g\n", ok ? "ok  " : "FAIL", set.name, n, worst);
        if (!ok) ++failures;
    }
}

static void test_simd_against_scalar() {
    std::printf("     simd set in use: %s\n", core::simd_set_name());
    test_simd_set_against_scalar({"selected set", core::power_db});
#if defined(__x86_64__)
    // Rosetta runs AVX2 code, but its CPU check reports no AVX2.
    if (std::getenv("TEST_AVX2")) test_simd_set_against_scalar({"avx2", core::avx2::power_db});
#endif
}

static void test_db_phase_time() {
    const core::Measurement& m = sample();
    size_t n = m.points();
    std::vector<double> out(n);
    core::TimeResponsePlan plan = core::make_time_response_plan(core::TimeResponseSpec{}, m.freq_hz.data(), n);
    expect_close("time axis ns", plan.t_ns.data(), reference("t_ns"), 1e-9);
    for (const char* p : {"s11", "s21"}) {
        size_t k = index_of(p);
        core::power_db(m.re[k].data(), m.im[k].data(), out.data(), n);
        expect_close(std::string("dB ") + p, out.data(), reference(std::string(p) + "_db"), 1e-10);
        core::phase_deg(m.re[k].data(), m.im[k].data(), out.data(), n);
        expect_close(std::string("phase ") + p, out.data(), reference(std::string(p) + "_deg"), 1e-10);
        core::time_response_db(plan, m.re[k].data(), m.im[k].data(), out.data());
        expect_close(std::string("time response dB ") + p, out.data(), reference(std::string(p) + "_time_db"), 1e-8);
    }
    std::vector<double> f = reference("odd_f_hz"), re = reference("odd_s21_re"), im = reference("odd_s21_im");
    core::TimeResponsePlan odd = core::make_time_response_plan(core::TimeResponseSpec{}, f.data(), f.size());
    out.resize(f.size());
    core::time_response_db(odd, re.data(), im.data(), out.data());
    expect_close("time axis ns, odd length", odd.t_ns.data(), reference("odd_t_ns"), 1e-9);
    expect_close("time response dB, odd length", out.data(), reference("odd_s21_time_db"), 1e-8);
}

static void test_time_response_level() {
    using namespace core;
    size_t n = 1001;
    double step = 1e6, delay = 20.0 / (static_cast<double>(n) * step);
    std::vector<double> f(n), re(n), im(n);
    for (size_t i = 0; i < n; ++i) {
        f[i] = 2e9 + step * static_cast<double>(i);
        re[i] = 0.5 * std::cos(-2.0 * pi * f[i] * delay);
        im[i] = 0.5 * std::sin(-2.0 * pi * f[i] * delay);
    }
    struct Case { const char* name; WindowSpec window; int pad; };
    const Case cases[] = {{"boxcar, no padding", {WindowKind::boxcar, 0, false}, 1},
                          {"hann, 4x padding", {WindowKind::hann, 0, false}, 4},
                          {"kaiser 9, 8x padding", {WindowKind::kaiser, 9, false}, 8}};
    for (const Case& c : cases) {
        TimeResponsePlan plan = make_time_response_plan({c.window, c.pad}, f.data(), n);
        std::vector<double> db(plan.t_ns.size());
        time_response_db(plan, re.data(), im.data(), db.data());
        size_t peak = 0;
        for (size_t k = 1; k < db.size(); ++k)
            if (db[k] > db[peak]) peak = k;
        bool ok = near(db[peak], 20.0 * std::log10(0.5), 1e-9) && near(plan.t_ns[peak], delay * 1e9, 1e-9);
        std::printf("%s time response level: %-25s peak %.6f dB at %.4f ns\n", ok ? "ok  " : "FAIL", c.name, db[peak], plan.t_ns[peak]);
        if (!ok) ++failures;
    }
}

static void test_gate_against_reference() {
    using namespace core;
    struct Case { const char* name; GateSpec spec; };
    GateSpec base;  // center 5, span 4, kaiser 6, kernel_convolution, bandpass, reflect
    auto with = [&](auto change) { GateSpec s = base; change(s); return s; };
    const Case cases[] = {
        {"gate_default", base},
        {"gate_center0", with([](GateSpec& s) { s.center_ns = 0.0; s.span_ns = 3.0; })},
        {"gate_negative", with([](GateSpec& s) { s.center_ns = -2.5; s.span_ns = 1.5; })},
        {"gate_wide", with([](GateSpec& s) { s.center_ns = 37.0; s.span_ns = 20.0; })},
        {"gate_hann", with([](GateSpec& s) { s.window = {WindowKind::hann, 0, true}; })},
        {"gate_kaiser12", with([](GateSpec& s) { s.window = {WindowKind::kaiser, 12, true}; })},
        {"gate_tukey", with([](GateSpec& s) { s.window = {WindowKind::tukey, 0.3, true}; })},
        {"gate_bandstop", with([](GateSpec& s) { s.mode = GateMode::bandstop; })},
        {"gate_nearest", with([](GateSpec& s) { s.edge = GateEdge::nearest; })},
        {"gate_wrap", with([](GateSpec& s) { s.edge = GateEdge::wrap; })},
        {"gate_zero", with([](GateSpec& s) { s.edge = GateEdge::zero; })},
    };
    const Measurement& m = sample();
    size_t n = m.points();
    std::vector<double> out_re(n), out_im(n);
    for (const Case& c : cases) {
        GatePlan plan = make_gate_plan(c.spec, m.freq_hz.data(), n);
        for (const char* p : {"s11", "s21"}) {
            apply_gate(plan, m.re[index_of(p)].data(), m.im[index_of(p)].data(), out_re.data(), out_im.data());
            std::string name = std::string(c.name) + "_" + p;
            expect_close(name + " re", out_re.data(), reference(name + "_re"), 1e-11);
            expect_close(name + " im", out_im.data(), reference(name + "_im"), 1e-11);
        }
    }
    std::vector<double> f = reference("odd_f_hz"), re = reference("odd_s21_re"), im = reference("odd_s21_im");
    for (const Case& c : cases) {
        std::string name = std::string("odd_") + c.name + "_s21";
        if (!std::filesystem::exists(std::string(REFERENCE_DIR) + "/" + name + "_re.f64")) continue;
        GatePlan plan = make_gate_plan(c.spec, f.data(), f.size());
        apply_gate(plan, re.data(), im.data(), out_re.data(), out_im.data());
        expect_close(name + " re", out_re.data(), reference(name + "_re"), 1e-11);
        expect_close(name + " im", out_im.data(), reference(name + "_im"), 1e-11);
    }
}

static void test_gate_time_multiply() {
    using namespace core;
    size_t n = 1001;
    double step = 1e6, dt = 1.0 / (static_cast<double>(n) * step);
    double delay_a = 9.0 * dt, delay_b = 27.0 * dt;
    std::vector<double> f(n), re(n), im(n), want_re(n), want_im(n), out_re(n), out_im(n);
    for (size_t i = 0; i < n; ++i) {
        f[i] = 2e9 + step * static_cast<double>(i);
        double a = -2.0 * pi * f[i] * delay_a, b = -2.0 * pi * f[i] * delay_b;
        want_re[i] = 0.5 * std::cos(a);
        want_im[i] = 0.5 * std::sin(a);
        re[i] = want_re[i] + 0.25 * std::cos(b);
        im[i] = want_im[i] + 0.25 * std::sin(b);
    }
    GateSpec spec;
    spec.method = GateMethod::time_multiply;
    spec.window = {WindowKind::boxcar, 0, false};
    spec.center_ns = delay_a * 1e9;
    spec.span_ns = 4.0 * dt * 1e9;
    GatePlan plan = make_gate_plan(spec, f.data(), n);
    apply_gate(plan, re.data(), im.data(), out_re.data(), out_im.data());
    expect_close("time_multiply keeps the gated echo, re", out_re.data(), want_re, 1e-10);
    expect_close("time_multiply keeps the gated echo, im", out_im.data(), want_im, 1e-10);
}

static void test_names() {
    using core::natural_less;
    using core::short_labels;
    using Names = std::vector<std::string>;
    expect("natural order: 2 before 10", natural_less("cable_2-AB", "cable_10-AB") && !natural_less("cable_10-AB", "cable_2-AB"));
    expect("natural order: 0mm before 100mm before 1000mm", natural_less("0mm_a", "100mm_a") && natural_less("100mm_a", "1000mm_a"));
    expect("natural order: equal names", !natural_less("a1", "a1"));

    Names one_folder(3, "/data/session");
    expect("labels: shared words dropped", short_labels({"800mm_cable_25-AB", "900mm_cable_3-AB", "900mm_cable_4-AB"}, one_folder) == Names({"800mm 25", "900mm 3", "900mm 4"}));
    expect("labels: a decimal number is one word", short_labels({"0.5mm_x", "1.5mm_x"}, {"/d", "/d"}) == Names({"0.5mm", "1.5mm"}));
    expect("labels: names of different shapes", short_labels({"800mm_cable_25-AB", "test-cable-AB", "empty_cable_1_b-AB"}, one_folder) == Names({"800mm 25", "test", "empty 1 b"}));
    expect("labels: same name in two folders gets the folder", short_labels({"800mm_1", "800mm_1"}, {"/a/15.12.2025 set", "/a/18.12.2025 set"}) == Names({"15.12.2025", "18.12.2025"}));
    expect("labels: unchanged when nothing is shared", short_labels({"a_1", "1_a", "b_2"}, one_folder) == Names({"a_1", "1_a", "b_2"}));
    expect("labels: unchanged when shortening would merge two", short_labels({"m_a_1", "m_1_a", "z_q_a_1"}, one_folder) == Names({"m_a_1", "m_1_a", "z_q_a_1"}));
    expect("labels: a single name is left alone", short_labels({"800mm_cable_25-AB"}, {"/d"}) == Names({"800mm_cable_25-AB"}));
}

static void test_order() {
    using Directions = std::vector<int8_t>;
    // Samples: rising order, falling order, low and middle exchanged, falling order with low and middle exchanged.
    const double low[] = {1, 9, 2, 5}, middle[] = {2, 5, 1, 9}, high[] = {3, 1, 3, 1};
    std::vector<const double*> y = {low, middle, high};
    expect("order: rises, falls, not ordered", core::ordered_samples(y, {10, 20, 30}, 4, 0) == Directions({1, -1, 0, 0}));
    expect("order: a rank difference of 1 is allowed", core::ordered_samples(y, {10, 20, 30}, 4, 1) == Directions({1, -1, 1, -1}));
    expect("order: curves in a different sequence in the list", core::ordered_samples({high, low, middle}, {30, 10, 20}, 4, 0) == Directions({1, -1, 0, 0}));
    expect("order: equal numbers give no order", core::ordered_samples(y, {10, 10, 30}, 4, 0) == Directions({1, -1, 1, -1}));
    expect("order: all numbers equal", core::ordered_samples(y, {10, 10, 10}, 4, 0) == Directions({0, 0, 0, 0}));

    const double flat[] = {2, 2, 2, 2}, tied[] = {2, 5, 7, 2};
    expect("order: equal values do not break the order", core::ordered_samples({tied, flat, high}, {10, 20, 30}, 4, 0) == Directions({1, -1, 0, -1}) &&
                                                         core::ordered_samples({flat, tied, high}, {10, 20, 30}, 4, 0) == Directions({1, 0, 0, -1}));

    const double outlier[] = {0, 0, 9, 9};  // number 25, in the wrong position in the first two samples
    core::Ordered some = core::ordered_samples_without_some({low, middle, high, outlier}, {10, 20, 30, 25}, 2, 0, 1);
    expect("order: the curve that breaks the order is left out", some.left_out == std::vector<size_t>({3}) && some.direction == Directions({1, -1}));
    some = core::ordered_samples_without_some(y, {10, 20, 30}, 2, 0, 2);
    expect("order: no curve is left out if the order is complete", some.left_out.empty() && some.direction == Directions({1, -1}));

    auto values = [](const std::string& name) {
        std::vector<double> v;
        for (const core::NumberInName& number : core::numbers_in(name)) v.push_back(number.value);
        return v;
    };
    expect("numbers in a name", values("800mm_cable_25-AB") == std::vector<double>({800, 25}));
    expect("numbers in a name: decimal point and decimal comma", values("0.5mm_1,25mm_x") == std::vector<double>({0.5, 1.25}));
    expect("numbers in a name: only the first point in a number is a decimal mark", values("18.12.2025") == std::vector<double>({18.12, 2025}));
    core::NumberInName second = core::numbers_in("800mm_cable_25-AB")[1];
    expect("numbers in a name: position", second.begin == 12 && second.end == 14);
}

static void test_touchstone_text() {
    bool same = true;
    for (int ports : {1, 2, 4}) {
        core::Measurement m;
        m.ports = ports;
        m.freq_hz = {1e9 / 3.0, 2e9 / 3.0, 1e9};
        m.re.assign(static_cast<size_t>(ports * ports), std::vector<double>(3));
        m.im = m.re;
        for (size_t k = 0; k < m.re.size(); ++k)
            for (size_t i = 0; i < 3; ++i) {
                m.re[k][i] = std::sin(static_cast<double>(7 * k + i + 1)) / 3.0;
                m.im[k][i] = -std::cos(static_cast<double>(5 * k + i + 2)) * 1e-7;
            }
        std::string text = core::touchstone_text(m);
        core::LoadResult r = core::load_measurement_from_text("x.s" + std::to_string(ports) + "p", text);
        same = same && r.error.empty() && r.measurement.freq_hz == m.freq_hz && r.measurement.re == m.re && r.measurement.im == m.im;
    }
    expect("touchstone text: a load gives the same numbers, bit for bit, for 1, 2 and 4 ports", same);
}

static void test_median() {
    const double x[] = {1, 2, 3, 4, 5, 6}, y[] = {9, 1, 5, 3, 7, 100};
    bool found;
    expect("median: odd count", near(core::median_in_range(x, y, 6, 1.0, 5.0, found), 5.0) && found);
    expect("median: even count is the mean of the two middle values", near(core::median_in_range(x, y, 6, 1.5, 5.5, found), 4.0) && found);
    expect("median: one sample", near(core::median_in_range(x, y, 6, 5.5, 6.5, found), 100.0) && found);
    core::median_in_range(x, y, 6, 2.2, 2.8, found);
    expect("median: no sample in the range", !found);
}

int main() {
    test_median();
    test_loader_against_reference();
    test_loader_formats();
    test_number_parsing();
    test_fourier();
    test_windows();
    test_simd_against_scalar();
    test_db_phase_time();
    test_time_response_level();
    test_gate_against_reference();
    test_gate_time_multiply();
    test_names();
    test_touchstone_text();
    test_order();
    std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
