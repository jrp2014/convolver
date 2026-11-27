// SPDX-License-Identifier: GPL-2.0-or-later
// Private: paths as UTF-8 text and UTF-8 text as paths (DIAG-7). Messages,
// logs and window strings are UTF-8 everywhere. std::filesystem::path's
// string() and its constructor from std::string use the native narrow
// encoding instead, which on Windows is the ANSI code page: string() throws
// for a name with characters outside it, and the constructor misreads UTF-8.
// Use these, not string() or path(std::string), wherever text meets a path.
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

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
// CFG-3: the length of the well-formed UTF-8 character at the start of
// `text` (1 to 4 bytes), or 0 when its first byte does not start one:
// a stray continuation byte, a truncated sequence, an overlong form, a
// surrogate or a value above U+10FFFF (RFC 3629).
inline std::size_t utf8_character_length(std::string_view text) noexcept {
    if (text.empty())
        return 0;
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(0);
    if (lead < 0x80)
        return 1;
    std::size_t length = 0;
    unsigned char low = 0x80, high = 0xbf; // Range of the second byte.
    if (lead >= 0xc2 && lead <= 0xdf)
        length = 2;
    else if (lead >= 0xe0 && lead <= 0xef) {
        length = 3;
        if (lead == 0xe0)
            low = 0xa0; // Not overlong.
        if (lead == 0xed)
            high = 0x9f; // Not a surrogate.
    } else if (lead >= 0xf0 && lead <= 0xf4) {
        length = 4;
        if (lead == 0xf0)
            low = 0x90; // Not overlong.
        if (lead == 0xf4)
            high = 0x8f; // At most U+10FFFF.
    } else
        return 0;
    if (text.size() < length || byte(1) < low || byte(1) > high)
        return 0;
    for (std::size_t i = 2; i < length; ++i)
        if (byte(i) < 0x80 || byte(i) > 0xbf)
            return 0;
    return length;
}
// Whether `text` is entirely well-formed UTF-8.
inline bool valid_utf8(std::string_view text) noexcept {
    while (!text.empty()) {
        const auto length = utf8_character_length(text);
        if (!length)
            return false;
        text.remove_prefix(length);
    }
    return true;
}
// `text` as UTF-8 for a message: each byte that is not part of a
// well-formed character is written as \xHH, so the message stays valid
// UTF-8 and shows the byte that is wrong.
inline std::string utf8_escaped(std::string_view text) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    while (!text.empty()) {
        if (const auto length = utf8_character_length(text)) {
            result.append(text.substr(0, length));
            text.remove_prefix(length);
            continue;
        }
        const auto b = static_cast<unsigned char>(text.front());
        result += "\\x";
        result += digits[b >> 4];
        result += digits[b & 15];
        text.remove_prefix(1);
    }
    return result;
}
// IO-13: why a write to `path` failed, when the reason is its length: for
// programs that do not declare themselves long-path aware, as these do not,
// Windows refuses a file whose full path is over 259 characters (UTF-16
// units) and a folder whose full path is over 247. Empty when the path is
// within that limit, and always elsewhere.
inline std::string windows_path_length_problem(const std::filesystem::path& path,
                                               bool folder = false) {
#ifdef _WIN32
    std::error_code error;
    const auto full = std::filesystem::absolute(path, error);
    const std::size_t length = (error ? path : full).native().size();
    const std::size_t limit = folder ? 247 : 259;
    if (length > limit)
        return "Its full path is " + std::to_string(length) + " characters long, and Windows " +
               "allows " + std::to_string(limit) + " at most for a " +
               (folder ? "folder." : "file.");
#else
    (void)path;
    (void)folder;
#endif
    return {};
}
// What to do about windows_path_length_problem().
inline constexpr std::string_view windows_path_length_fix =
    "Choose a shorter name, or a folder with a shorter path.";
// UTF-8 text as a path.
inline std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
} // namespace convolver::detail
