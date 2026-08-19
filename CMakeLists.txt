cmake_minimum_required(VERSION 3.24)
project(jade_compiler
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION ".JADE — a 4-tier profile-driven JIT compiler for a dynamic language"
)

# ─────────────────────────────────────────────────────────────────────────────
# Global settings
# ─────────────────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Default install path: project-local so non-root users can install
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

# Output to a single bin/ dir for easier testing
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

enable_testing()

# ─────────────────────────────────────────────────────────────────────────────
# Options
# ─────────────────────────────────────────────────────────────────────────────
option(JADE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(JADE_ENABLE_SANITIZERS "Enable ASan/UBSan in debug builds" OFF)
option(JADE_BUILD_TESTS "Build unit tests" ON)
option(JADE_BUILD_BENCHMARKS "Build benchmarks" ON)
option(JADE_USE_LTO "Enable link-time optimization in Release" ON)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler flags
# ─────────────────────────────────────────────────────────────────────────────
include(${CMAKE_SOURCE_DIR}/cmake/CompilerFlags.cmake)

# ─────────────────────────────────────────────────────────────────────────────
# Subdirectories
# ─────────────────────────────────────────────────────────────────────────────
add_subdirectory(src/jade)

if(JADE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_subdirectory(tests)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Benchmarks
# ─────────────────────────────────────────────────────────────────────────────
if(JADE_BUILD_BENCHMARKS)
    add_executable(jade_bench benchmarks/jade/benchmark.cpp)
    jade_target_apply_tooling_flags(jade_bench)
    target_link_libraries(jade_bench PRIVATE jade_core jade_granit)
    if(TARGET jade_tier1)
        target_link_libraries(jade_bench PRIVATE jade_tier1)
        target_compile_definitions(jade_bench PRIVATE JADE_HAVE_ASMJIT=1)
    endif()

    # Loop benchmark: emits native x86-64 loops to match .NET per-iteration cost.
    add_executable(jade_loop_bench benchmarks/jade/loop_benchmark.cpp)
    jade_target_apply_tooling_flags(jade_loop_bench)
    target_link_libraries(jade_loop_bench PRIVATE jade_core jade_granit)
    if(TARGET jade_tier1)
        target_link_libraries(jade_loop_bench PRIVATE jade_tier1)
        target_compile_definitions(jade_loop_bench PRIVATE JADE_HAVE_ASMJIT=1)
    endif()
    if(TARGET asmjit::asmjit)
        target_link_libraries(jade_loop_bench PRIVATE asmjit::asmjit)
    endif()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Status message
# ─────────────────────────────────────────────────────────────────────────────
message(STATUS "========================= .JADE =========================")
message(STATUS "  Version            : ${PROJECT_VERSION}")
message(STATUS "  Build type         : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard       : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler           : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Warnings as errors : ${JADE_WARNINGS_AS_ERRORS}")
message(STATUS "  Sanitizers         : ${JADE_ENABLE_SANITIZERS}")
message(STATUS "  Tests              : ${JADE_BUILD_TESTS}")
message(STATUS "  LTO                : ${JADE_USE_LTO}")
message(STATUS "========================================================")
