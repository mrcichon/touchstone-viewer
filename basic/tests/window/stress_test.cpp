#include "stress_test.h"

#include "line_renderer.h"
#include "log.h"
#include "parallel.h"
#include "paths.h"
#include "platform.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {

const int curve_counts[] = {100, 300, 1000, 3000, 6000, 10000};
const size_t points_in_curve = 1001;
const char* stage_names[] = {"frequency, 2 panels", "time, gate off, 4 panels", "time, gate on, 6 panels"};

bool running = false, last_step = false;
std::string report;
size_t step = 0;
int stage = 0;
int requested = 0;
int frames = 0, frames_to_measure = 0;
double first_ms = 0.0;
std::vector<double> times;
size_t segments_before = 0;

core::Measurement generated_measurement(int number) {
    core::Measurement m;
    m.path = "generated/curve " + std::to_string(number) + ".s2p";
    m.ports = 2;
    m.re.assign(4, std::vector<double>(points_in_curve));
    m.im = m.re;
    uint32_t random = static_cast<uint32_t>(number) * 2654435761u + 1u;
    auto noise = [&] {
        random = random * 1664525u + 1013904223u;
        return static_cast<double>(random >> 8) / 16777216.0 - 0.5;
    };
    const double two_pi = 6.283185307179586, delay_s = (2.0 + 0.002 * number) * 1e-9;
    for (size_t i = 0; i < points_in_curve; ++i) {
        double f = 1e9 + 2e9 * static_cast<double>(i) / static_cast<double>(points_in_curve - 1);
        m.freq_hz.push_back(f);
        for (size_t k = 0; k < 4; ++k) {
            bool through = k == 1 || k == 2;
            double level = through ? 0.003 * (1.0 + 0.5 * std::sin(two_pi * f * 8e-9)) : 0.25 * (1.0 + 0.6 * std::sin(two_pi * f * 5e-9 + number));
            level *= 1.0 + 0.2 * noise();
            double phase = -two_pi * f * delay_s * (through ? 3.0 : 1.0);
            m.re[k][i] = level * std::cos(phase);
            m.im[k][i] = level * std::sin(phase);
        }
    }
    return m;
}

void add_curves(Session& session, int first, int end) {
    std::vector<std::unique_ptr<Curve>> curves(static_cast<size_t>(end - first));
    core::run_in_parallel(curves.size(), [&](size_t i) { curves[i] = make_curve(generated_measurement(first + static_cast<int>(i))); });
    std::lock_guard<std::mutex> hold(session.loader.lock);
    for (auto& c : curves) session.loader.arrived.push_back(std::move(c));
}

CHECKED_AS_PRINTF(1, 2) void add_line(const char* format, ...) {
    char line[256];
    va_list values;
    va_start(values, format);
    std::vsnprintf(line, sizeof line, format, values);
    va_end(values);
    report += line;
    report += '\n';
}

void finish() {
    running = false;
    report += "first: the slowest of the first 3 frames after the change. It includes the calculation and the transfer of the points.\n"
              "segments: line segments drawn in one frame. memory: of the program. graphics: memory reserved for points.\n";
    std::fputs(report.c_str(), stdout);
    std::filesystem::path path = core::path_from_utf8(log_file_path()).parent_path() / "touchstone-viewer-stress.txt";
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (std::ofstream(path, std::ios::binary) << report) std::printf("written to %s\n", core::utf8_from_path(path).c_str());
    log_line("%s", report.c_str());
}

}  // namespace

void start_stress_test(const std::string& system_lines) {
    running = true;
    report = "Touchstone viewer stress test: generated two-port files, " + std::to_string(points_in_curve) + " points each\n" + system_lines;
    add_line("%7s  %-26s %12s %9s %10s %9s %10s %12s", "curves", "view", "segments", "first ms", "median ms", "worst ms", "memory MB", "graphics MB");
}

bool stress_test_running() { return running; }

void stress_before_frame(Session& session) {
    if (!running || requested >= curve_counts[step]) return;
    add_curves(session, requested, curve_counts[step]);
    requested = curve_counts[step];
}

void stress_after_frame(Session& session, double frame_ms) {
    if (!running || static_cast<int>(session.curves.size()) < requested) return;
    if (++frames <= 3) {
        first_ms = std::max(first_ms, frame_ms);
        frames_to_measure = std::clamp(static_cast<int>(3000.0 / std::max(frame_ms, 1.0)), 5, 30);
        segments_before = line_renderer::totals().segments_drawn;
        return;
    }
    times.push_back(frame_ms);
    if (static_cast<int>(times.size()) < frames_to_measure) return;

    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];
    line_renderer::Totals totals = line_renderer::totals();
    add_line("%7d  %-26s %12zu %9.1f %10.1f %9.1f %10.0f %12.0f", requested, stage_names[stage], (totals.segments_drawn - segments_before) / times.size(), first_ms, median,
             times.back(), memory_in_use_mb(), static_cast<double>(totals.bytes_reserved) / (1024.0 * 1024.0));
    last_step = last_step || median > 1000.0 || step + 1 == std::size(curve_counts);
    frames = 0;
    first_ms = 0.0;
    times.clear();

    stage = (stage + 1) % 3;
    session.view = stage == 0 ? View::frequency : View::time;
    session.gate_on = stage == 2;
    ++session.time_settings_version;
    if (stage != 0) return;
    if (last_step) finish();
    else ++step;
}
