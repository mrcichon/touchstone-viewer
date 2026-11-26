#include "log.h"

#include "paths.h"
#include "platform.h"

#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

std::FILE* file = nullptr;
std::mutex lock;
const auto started = std::chrono::steady_clock::now();

#if defined(_WIN32)
// To read an address: x86_64-w64-mingw32-addr2line -f -C -e touchstone_viewer.exe.debug 0x140000000+ADDRESS
unsigned long long relative_to_program(const void* address) {
    return reinterpret_cast<unsigned long long>(address) - reinterpret_cast<unsigned long long>(GetModuleHandleW(nullptr));
}

void write_call_stack() {
    void* frames[48];
    USHORT count = CaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) std::fprintf(file, "  program + 0x%llx\n", relative_to_program(frames[i]));
    std::fflush(file);
}

LONG WINAPI on_crash(EXCEPTION_POINTERS* info) {
    if (!file) return EXCEPTION_CONTINUE_SEARCH;
    const EXCEPTION_RECORD* record = info->ExceptionRecord;
    const DWORD code = record->ExceptionCode;
    const char* name = code == EXCEPTION_ACCESS_VIOLATION      ? "invalid memory access"
                     : code == EXCEPTION_STACK_OVERFLOW        ? "stack overflow"
                     : code == EXCEPTION_ILLEGAL_INSTRUCTION   ? "illegal instruction"
                     : code == EXCEPTION_INT_DIVIDE_BY_ZERO    ? "integer division by zero"
                     : code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED ? "array index out of range"
                     : code == 0xE06D7363                      ? "C++ exception"
                                                               : "other";
    std::fprintf(file, "crash: exception 0x%08lx (%s) at program + 0x%llx\n", static_cast<unsigned long>(code), name, relative_to_program(record->ExceptionAddress));
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
        std::fprintf(file, "  %s address 0x%llx\n", record->ExceptionInformation[0] ? "write to" : "read of", static_cast<unsigned long long>(record->ExceptionInformation[1]));
    write_call_stack();
    return EXCEPTION_CONTINUE_SEARCH;
}

// A failed check of the C++ library ends in abort, which does not go through on_crash.
void on_abort(int) {
    if (file) {
        std::fprintf(file, "crash: abort; a check of the C++ library failed, or std::terminate was called\n");
        write_call_stack();
    }
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
}
#endif

#if !defined(_WIN32)
// Only functions that are safe in a signal handler: write and backtrace_symbols_fd.
void on_crash(int signal_number) {
    int fd = file ? fileno(file) : STDERR_FILENO;
    const char* name = signal_number == SIGSEGV ? "crash: SIGSEGV (invalid memory access)\n"
                     : signal_number == SIGABRT ? "crash: SIGABRT (abort; failed check or out of memory)\n"
                     : signal_number == SIGBUS  ? "crash: SIGBUS (invalid memory access)\n"
                     : signal_number == SIGFPE  ? "crash: SIGFPE (arithmetic error)\n"
                                                : "crash: SIGILL (illegal instruction)\n";
    size_t length = 0;
    while (name[length]) ++length;
    (void)!write(fd, name, length);
    void* frames[64];
    int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, fd);
    std::signal(signal_number, SIG_DFL);
    std::raise(signal_number);
}
#endif

void on_unhandled_exception() {
    try {
        if (std::exception_ptr current = std::current_exception()) std::rethrow_exception(current);
        log_line("crash: std::terminate without an exception");
    } catch (const std::exception& e) {
        log_line("crash: unhandled exception: %s", e.what());
    } catch (...) {
        log_line("crash: unhandled exception of unknown type");
    }
    std::abort();
}

}  // namespace

std::string start_log() {
    std::string path = log_file_path();
    std::error_code error;
    std::filesystem::path p = core::path_from_utf8(path);
    std::filesystem::create_directories(p.parent_path(), error);
    std::filesystem::rename(p, core::path_from_utf8(path + ".previous"), error);
#if defined(_WIN32)
    file = _wfopen(p.c_str(), L"w");
    SetUnhandledExceptionFilter(on_crash);
    std::signal(SIGABRT, on_abort);
#else
    file = std::fopen(path.c_str(), "w");
    for (int signal_number : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) std::signal(signal_number, on_crash);
#endif
    std::set_terminate(on_unhandled_exception);
    return path;
}

void log_line(const char* format, ...) {
    if (!file) return;
    std::lock_guard<std::mutex> hold(lock);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::fprintf(file, "%10.3f  ", seconds);
    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);
    std::fputc('\n', file);
    std::fflush(file);  // on disk before the next operation, which can crash
}
