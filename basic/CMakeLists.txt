cmake_minimum_required(VERSION 3.24)
project(touchstone_viewer_basic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(SANITIZE "address and undefined-behaviour sanitizers (on by default in Debug)" OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MINGW)
  set(SANITIZE ON)
endif()
if(MINGW)
  add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)  # the printf of the old Windows C library does not know %zu
endif()

function(use_strict_flags target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wconversion -Wsign-conversion -Wdouble-promotion -Wfloat-equal
    -Wold-style-cast -Wcast-align -Wcast-qual -Wuseless-cast
    -Wnull-dereference -Wformat=2 -Wimplicit-fallthrough -Wundef
    -Wnon-virtual-dtor -Woverloaded-virtual -Wextra-semi -Wunused -Wmissing-declarations
    -ffp-contract=off  # a*b+c is fused only where the code says so: results do not depend on the optimiser
    -fno-math-errno)   # the compiler can then inline sqrt
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE -Wno-unknown-warning-option -Wshorten-64-to-32 -Wunreachable-code -Wcomma)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    target_compile_options(${target} PRIVATE -Wno-null-dereference)  # false alarm inside std::copy
  endif()
  # Checked standard library: an index out of range stops the program.
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${target} PRIVATE _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG _GLIBCXX_ASSERTIONS)
  else()
    target_compile_definitions(${target} PRIVATE _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST _GLIBCXX_ASSERTIONS)
  endif()
  if(SANITIZE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

add_library(core STATIC
  core/load.cpp core/number.cpp core/fourier.cpp core/window.cpp core/time_domain.cpp core/simd.cpp core/names.cpp core/median.cpp core/order.cpp)
target_include_directories(core PUBLIC core)
use_strict_flags(core)
# The SIMD file follows the target of the compiler, not the CPU of the build machine.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#if !defined(__aarch64__)\n#error\n#endif\nint main() {}" compiler_targets_arm64)
check_cxx_source_compiles("#if !defined(__x86_64__)\n#error\n#endif\nint main() {}" compiler_targets_x86_64)
if(compiler_targets_arm64)
  target_sources(core PRIVATE core/simd_neon.cpp)
elseif(compiler_targets_x86_64)
  target_sources(core PRIVATE core/simd_avx2.cpp)
  set_source_files_properties(core/simd_avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx2;-mfma")  # this file only
endif()
find_package(Threads REQUIRED)
target_link_libraries(core PUBLIC Threads::Threads)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE core)
target_compile_definitions(test_core PRIVATE REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")
use_strict_flags(test_core)
enable_testing()
add_test(NAME core COMMAND test_core)

add_executable(time_core tests/time_core.cpp)
target_link_libraries(time_core PRIVATE core)
target_compile_definitions(time_core PRIVATE REFERENCE_DIR="${CMAKE_SOURCE_DIR}/tests/reference")
use_strict_flags(time_core)

option(CORE_ONLY "build the core library and its tests without the window" OFF)
if(NOT CORE_ONLY)
  add_subdirectory(app)
endif()
