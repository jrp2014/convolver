// SPDX-License-Identifier: GPL-2.0-or-later
// Private: paths as UTF-8 text and UTF-8 text as paths (DIAG-7). Messages,
// logs and window strings are UTF-8 everywhere. std::filesystem::path's
// string() and its constructor from std::string use the native narrow
// encoding instead, which on Windows is the ANSI code page: string() throws
// for a name with characters outside it, and the constructor misreads UTF-8.
// Use these, not string() or path(std::string), wherever text meets a path.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace convolver::detail {
// The path in its native form (\ on Windows), as UTF-8.
inline std::string utf8_string(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
// The path with / separators on every system, as UTF-8.
inline std::string utf8_generic_string(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}
// UTF-8 text as a path.
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace convolver::detail
