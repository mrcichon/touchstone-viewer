#pragma once
#include <string>

#if defined(__MINGW32__)
#define CHECKED_AS_PRINTF(format_at, arguments_at) __attribute__((format(gnu_printf, format_at, arguments_at)))
#elif defined(__GNUC__)
#define CHECKED_AS_PRINTF(format_at, arguments_at) __attribute__((format(printf, format_at, arguments_at)))
#else
#define CHECKED_AS_PRINTF(format_at, arguments_at)
#endif

std::string start_log();
CHECKED_AS_PRINTF(1, 2) void log_line(const char* format, ...);
