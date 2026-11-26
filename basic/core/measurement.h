#pragma once
#include <string>
#include <vector>

namespace core {

struct Measurement {
    std::string path;
    int ports = 0;
    bool has_phase = true;  // false for csv: magnitude only
    std::vector<double> freq_hz;
    std::vector<std::vector<double>> re, im;  // [parameter][point]

    size_t points() const { return freq_hz.size(); }
    int parameter(int out_port, int in_port) const { return (out_port - 1) * ports + (in_port - 1); }  // S21: out 2, in 1
};

struct LoadResult {
    Measurement measurement;
    std::string error;
};

enum class CsvRule { any, analyzer_export_only };
enum class FileKind { other, touchstone, csv };

FileKind file_kind(const std::string& path);
LoadResult load_measurement(const std::string& path, CsvRule csv = CsvRule::any);
bool read_whole_file(const std::string& path, std::string& text);
LoadResult load_measurement_from_text(const std::string& path, std::string& text, CsvRule csv = CsvRule::any);  // changes text

std::string touchstone_text(const Measurement& m);  // 17 digits: loads back bit for bit

}  // namespace core
