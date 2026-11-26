#include "measurement.h"

#include "number.h"
#include "paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>
#include <utility>

namespace core {
namespace {

using std::numbers::ln10;
using std::numbers::pi;

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string lowercase(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string extension_of(const std::string& path) {
    size_t dot = path.find_last_of('.');
    size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return lowercase(path.substr(dot + 1));
}

int ports_from_extension(const std::string& ext) {
    if (ext.size() < 3 || ext.front() != 's' || ext.back() != 'p') return 0;
    int ports = 0;
    for (size_t i = 1; i + 1 < ext.size(); ++i) {
        if (ext[i] < '0' || ext[i] > '9') return 0;
        ports = ports * 10 + (ext[i] - '0');
    }
    return ports;
}

void append_numbers(const char* p, const char* end, std::vector<double>& out) {
    while (p < end) {
        if (is_space(*p)) {
            ++p;
            continue;
        }
        double value;
        if (parse_number(p, end, value)) {
            out.push_back(value);
        } else {
            while (p < end && !is_space(*p)) ++p;
        }
    }
}

enum class NumberFormat { real_imag, magnitude_angle, db_angle };

struct OptionLine {
    double freq_scale = 1e9;  // the default unit of Touchstone is GHz
    NumberFormat format = NumberFormat::magnitude_angle;
    char parameter = 's';
};

// "# GHz S DB R 50": unit, parameter type, number format, reference resistance, in any order.
OptionLine parse_option_line(const std::string& line) {
    OptionLine o;
    std::string token;
    auto take = [&](const std::string& t) {
        if (t == "hz") o.freq_scale = 1.0;
        else if (t == "khz") o.freq_scale = 1e3;
        else if (t == "mhz") o.freq_scale = 1e6;
        else if (t == "ghz") o.freq_scale = 1e9;
        else if (t == "ri") o.format = NumberFormat::real_imag;
        else if (t == "ma") o.format = NumberFormat::magnitude_angle;
        else if (t == "db") o.format = NumberFormat::db_angle;
        else if (t == "s" || t == "y" || t == "z" || t == "g" || t == "h") o.parameter = t[0];
    };
    for (char c : lowercase(line)) {
        if (is_space(c) || c == '#') {
            if (!token.empty()) take(token);
            token.clear();
        } else {
            token += c;
        }
    }
    if (!token.empty()) take(token);
    return o;
}

LoadResult failed(const std::string& why) {
    LoadResult r;
    r.error = why;
    return r;
}

LoadResult load_touchstone(const std::string& path, int ports, std::string& text) {
    OptionLine option;
    bool seen_option = false;
    bool order_21_12 = (ports == 2);  // Touchstone 1.x two-port order: S11 S21 S12 S22
    bool in_network_data = true;      // 2.0: other keyword sections, such as [Reference], also contain numbers
    std::vector<double> numbers;

    char* p = text.data();
    char* end = p + text.size();
    while (p < end) {
        char* line_end = std::find(p, end, '\n');
        char* content_end = std::find(p, line_end, '!');
        char* first = p;
        while (first < content_end && is_space(*first)) ++first;
        p = line_end < end ? line_end + 1 : end;
        if (first == content_end) continue;

        if (*first == '#') {
            if (!seen_option) option = parse_option_line(std::string(first, content_end));
            seen_option = true;
        } else if (*first == '[') {
            std::string keyword = lowercase(std::string(first, content_end));
            auto has = [&](const char* text_in_keyword) { return keyword.find(text_in_keyword) != std::string::npos; };
            if (has("[version]")) in_network_data = false;
            else if (has("[network data]")) in_network_data = true;
            else if (has("[noise data]") || has("[end]")) break;
            else if (has("[two-port data order]") && has("12_21")) order_21_12 = false;
            else if (has("[matrix format]") && !has("full")) return failed("only the full matrix format is supported");
        } else if (in_network_data) {
            // Decimal comma of some instruments. Touchstone has no comma separator.
            std::replace(first, content_end, ',', '.');
            append_numbers(first, content_end, numbers);
        }
    }
    if (option.parameter != 's') return failed("only S-parameter files are supported");

    const size_t count = static_cast<size_t>(ports) * static_cast<size_t>(ports);
    const size_t per_point = 1 + 2 * count;
    size_t points = numbers.size() / per_point;
    bool whole_rows = numbers.size() % per_point == 0;
    for (size_t i = 1; i < points; ++i) {
        if (numbers[i * per_point] > numbers[(i - 1) * per_point]) continue;
        // In a 1.x two-port file the noise parameters follow the data, and the frequency starts again.
        if (ports != 2) return failed("frequency does not increase; the file extension possibly does not agree with the port count");
        points = i;
        whole_rows = true;
        break;
    }
    if (!whole_rows) return failed("the number count is not a multiple of the row length for this port count");
    if (points < 2) return failed("fewer than two frequency points");

    LoadResult r;
    Measurement& m = r.measurement;
    m.path = path;
    m.ports = ports;
    m.freq_hz.resize(points);
    m.re.assign(count, std::vector<double>(points));
    m.im.assign(count, std::vector<double>(points));
    for (size_t i = 0; i < points; ++i) {
        const double* row = &numbers[i * per_point];
        m.freq_hz[i] = row[0] * option.freq_scale;
        for (size_t k = 0; k < count; ++k) {
            double a = row[1 + 2 * k], b = row[2 + 2 * k];
            double re, im;
            if (option.format == NumberFormat::real_imag) {
                re = a;
                im = b;
            } else {
                double magnitude = option.format == NumberFormat::db_angle ? std::exp(a * (ln10 / 20.0)) : a;  // 10^(a/20)
                double angle = b * pi / 180.0;
                re = magnitude * std::cos(angle);
                im = magnitude * std::sin(angle);
            }
            size_t store = k;
            if (order_21_12 && (k == 1 || k == 2)) store = 3 - k;  // the file has S21 before S12
            m.re[store][i] = re;
            m.im[store][i] = im;
        }
    }
    return r;
}

// Frequency in Hz and level in dB: a spectrum analyzer export (rows after the "Trace Data" line), or two plain columns.
// The separator is ';' or tab if the file has one, and a comma is then a decimal comma. Otherwise it is ','.
LoadResult load_csv(const std::string& path, std::string& text, CsvRule rule) {
    size_t start = 0;
    size_t trace = text.find("Trace Data");
    if (trace == std::string::npos && rule == CsvRule::analyzer_export_only) return failed("not a spectrum analyzer export");
    if (trace != std::string::npos) start = std::min(text.find('\n', trace), text.size());
    char separator = text.find(';') != std::string::npos ? ';' : text.find('\t') != std::string::npos ? '\t' : ',';
    if (separator != ',') std::replace(text.begin(), text.end(), ',', '.');

    LoadResult r;
    Measurement& m = r.measurement;
    m.path = path;
    m.ports = 1;
    m.has_phase = false;
    std::vector<double> magnitude, fields;
    const char* p = text.data() + start;
    const char* end = text.data() + text.size();
    while (p < end) {
        const char* line_end = std::find(p, end, '\n');
        const char* cut = std::find(p, line_end, separator);
        fields.clear();
        append_numbers(p, cut, fields);
        size_t in_first = fields.size();
        if (cut < line_end) append_numbers(cut + 1, std::find(cut + 1, line_end, separator), fields);
        if (in_first == 1 && fields.size() == 2) {  // a row that is not two numbers is a header row
            m.freq_hz.push_back(fields[0]);
            magnitude.push_back(std::pow(10.0, fields[1] / 20.0));
        }
        p = line_end < end ? line_end + 1 : end;
    }
    if (m.freq_hz.size() < 2) return failed("fewer than two frequency,value rows");
    if (!std::is_sorted(m.freq_hz.begin(), m.freq_hz.end())) return failed("frequency does not increase");
    m.im.assign(1, std::vector<double>(magnitude.size(), 0.0));
    m.re.assign(1, std::move(magnitude));
    return r;
}

}  // namespace

FileKind file_kind(const std::string& path) {
    std::string ext = extension_of(path);
    if (ports_from_extension(ext) > 0) return FileKind::touchstone;
    return ext == "csv" ? FileKind::csv : FileKind::other;
}

bool read_whole_file(const std::string& path, std::string& text) {
    std::ifstream in(path_from_utf8(path), std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size < 0 || !std::in_range<std::streamsize>(size)) return false;  // 32-bit: 2 GB or more
    text.resize(static_cast<size_t>(size));
    in.seekg(0);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    return in.gcount() == size;
}

LoadResult load_measurement(const std::string& path, CsvRule csv) {
    std::string text;
    if (!read_whole_file(path, text)) return failed("cannot read the file");
    return load_measurement_from_text(path, text, csv);
}

LoadResult load_measurement_from_text(const std::string& path, std::string& text, CsvRule csv) {
    std::string ext = extension_of(path);
    if (int ports = ports_from_extension(ext)) return load_touchstone(path, ports, text);
    if (ext == "csv") return load_csv(path, text, csv);
    return failed("not a .sNp or .csv file");
}

std::string touchstone_text(const Measurement& m) {
    std::string text = "# Hz S RI R 50\n";
    char number[64];
    auto add = [&](double value) {
        std::snprintf(number, sizeof number, "%.17g", value);
        text += number;
    };
    const size_t ports = static_cast<size_t>(m.ports);
    for (size_t i = 0; i < m.points(); ++i) {
        add(m.freq_hz[i]);
        for (size_t k = 0; k < ports * ports; ++k) {
            size_t parameter = ports == 2 ? (k == 1 ? 2 : k == 2 ? 1 : k) : k;
            text += (ports > 2 && k > 0 && k % ports == 0) ? '\n' : ' ';
            add(m.re[parameter][i]);
            text += ' ';
            add(m.im[parameter][i]);
        }
        text += '\n';
    }
    return text;
}

}  // namespace core
