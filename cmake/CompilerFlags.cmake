# ─────────────────────────────────────────────────────────────────────────────
# CompilerFlags.cmake — enforces B.2 (no exceptions), B.3 (no RTTI) on JIT
# ─────────────────────────────────────────────────────────────────────────────

# Common warning flags for all C++ code in the project
set(JADE_COMMON_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wconversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wmisleading-indentation
    -Wduplicated-cond
    -Wduplicated-branches
    -Wlogical-op
    -Wuseless-cast
)

# Flags applied to the JIT hot path (B.1, B.2, B.3).
# No exceptions, no RTTI, no unwind tables — saves binary size and CPU.
set(JADE_HOT_PATH_FLAGS
    -fno-exceptions
    -fno-rtti
    -fno-unwind-tables
    -fno-asynchronous-unwind-tables
    -ffunction-sections
    -fdata-sections
)

# Defines that the JIT hot-path code can use to gate debug-only assertions
set(JADE_HOT_PATH_DEFINES
    JADE_HOT_PATH=1
)

if(JADE_WARNINGS_AS_ERRORS)
    set(JADE_COMMON_WARNINGS ${JADE_COMMON_WARNINGS} -Werror)
endif()

# Sanitizers — useful for catching the kind of bugs that violate Rule 42
if(JADE_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(JADE_SANITIZER_FLAGS
            -fsanitize=address,undefined
            -fsanitize-address-use-after-scope
            -fno-omit-frame-pointer
        )
    endif()
endif()

# Helper function: apply hot-path compile flags to a target
function(jade_target_apply_hot_path_flags target)
    target_compile_options(${target} PRIVATE
        ${JADE_COMMON_WARNINGS}
        ${JADE_HOT_PATH_FLAGS}
        ${JADE_SANITIZER_FLAGS}
    )
    target_compile_definitions(${target} PRIVATE ${JADE_HOT_PATH_DEFINES})
    target_include_directories(${target} PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    )
endfunction()

# Helper function: apply driver/tooling compile flags (exceptions allowed)
function(jade_target_apply_tooling_flags target)
    target_compile_options(${target} PRIVATE
        ${JADE_COMMON_WARNINGS}
        ${JADE_SANITIZER_FLAGS}
    )
    target_include_directories(${target} PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    )
endfunction()

# LTO for Release
if(JADE_USE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_output)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO requested but not supported: ${lto_output}")
    endif()
endif()

# Add -march=native for the host binary (not for the JIT-emitted code).
# The JIT code targets baseline x86-64; the host binary can use native.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-mtune=native)
endif()
