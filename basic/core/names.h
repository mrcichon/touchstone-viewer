#pragma once
#include <string>
#include <vector>

namespace core {

bool natural_less(const std::string& a, const std::string& b);  // "cable_2" < "cable_10"

// Drops the words that all names share: "800mm_cable_25-AB", "900mm_cable_3-AB" -> "800mm 25", "900mm 3".
std::vector<std::string> short_labels(const std::vector<std::string>& names, const std::vector<std::string>& folders);

struct NumberInName {
    double value;
    size_t begin, end;
};
std::vector<NumberInName> numbers_in(const std::string& name);  // "0.5mm_cable_25-AB" -> 0.5, 25

}  // namespace core
