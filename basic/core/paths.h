#pragma once
#include <filesystem>
#include <string>

// Path strings are UTF-8. On Windows a std::string path is read in the system code page.
namespace core {

inline std::filesystem::path path_from_utf8(const std::string& s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

inline std::string utf8_from_path(const std::filesystem::path& p) {
    std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace core
