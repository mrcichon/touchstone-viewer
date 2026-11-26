#pragma once

namespace core {

// Correctly rounded, no locale. On success moves p after the number. Refuses infinity and NaN.
bool parse_number(const char*& p, const char* end, double& value);

}  // namespace core
