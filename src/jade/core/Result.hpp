// SPDX-License-Identifier: MIT
// .JADE Compiler — core/Result.hpp
//
// Wrapper around std::expected<T,E> that obeys Rule B.2:
//   "No exceptions in the JIT hot path."
//
// All fallible JIT APIs return Result<T>. The driver may use exceptions for I/O;
// the JIT proper never does.

#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace jade {

// ─────────────────────────────────────────────────────────────────────────────
// Error — discriminates between recoverable and fatal compilation errors.
// ─────────────────────────────────────────────────────────────────────────────
enum class ErrorKind : uint8_t {
    // Recoverable: abort compilation, fall back to lower tier, keep running.
    InvalidIR,
    VerificationFailed,
    UnsupportedNode,
    OutOfBudget,
    ProfileUnavailable,
    DeoptMissingState,
    BadInput,

    // Fatal: the process should abort (only used by the driver).
    Internal,
    IO,
};

struct Error {
    ErrorKind kind{ErrorKind::Internal};
    std::string message;

    Error() = default;
    Error(ErrorKind k, std::string m) : kind(k), message(std::move(m)) {}

    auto what() const noexcept -> std::string_view { return message; }
};

// Convenience builders — prefer these over raw construction.
[[nodiscard]] inline auto make_error(ErrorKind k, std::string m) -> Error {
    return Error{k, std::move(m)};
}

[[nodiscard]] inline auto make_error_invalid_ir(std::string m) -> Error {
    return Error{ErrorKind::InvalidIR, std::move(m)};
}

[[nodiscard]] inline auto make_error_verification(std::string m) -> Error {
    return Error{ErrorKind::VerificationFailed, std::move(m)};
}

[[nodiscard]] inline auto make_error_unsupported(std::string m) -> Error {
    return Error{ErrorKind::UnsupportedNode, std::move(m)};
}

// ─────────────────────────────────────────────────────────────────────────────
// Result<T> — alias for std::expected.
// ─────────────────────────────────────────────────────────────────────────────
template <typename T>
using Result = std::expected<T, Error>;

// ─────────────────────────────────────────────────────────────────────────────
// Convenience: unexpected builder.
// ─────────────────────────────────────────────────────────────────────────────
template <typename... Args>
[[nodiscard]] auto unexpected_err(Args&&... args) -> std::unexpected<Error> {
    return std::unexpected<Error>(Error{std::forward<Args>(args)...});
}

}  // namespace jade
