// SPDX-License-Identifier: GPL-2.0-or-later
// Core Foundation helpers shared by the Audio Unit's implementation files.
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <memory>
#include <string>
#include <type_traits>

namespace convolver::au {
// Releases a Core Foundation value the holder owns (a Create or Copy result).
struct CfRelease {
    void operator()(CFTypeRef value) const noexcept {
        CFRelease(value);
    }
};
// An owned Core Foundation value, such as cf_owned<CFStringRef>, released
// when it goes; release() hands it to a caller that releases it.
template <class Ref> using cf_owned = std::unique_ptr<std::remove_pointer_t<Ref>, CfRelease>;

// UTF-8 text as a CFString; null if out of memory.
inline cf_owned<CFStringRef> cf_string(const std::string& text) {
    return cf_owned<CFStringRef>(
        CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false));
}

// A CFString's text as UTF-8; empty if `value` is not a string. Allocates, so
// it may throw std::bad_alloc.
inline std::string utf8_text(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    const auto text = static_cast<CFStringRef>(value);
    const auto range = CFRangeMake(0, CFStringGetLength(text));
    CFIndex used = 0;
    CFStringGetBytes(text, range, kCFStringEncodingUTF8, 0, false, nullptr, 0, &used);
    std::string out(static_cast<std::size_t>(used), '\0');
    CFStringGetBytes(text, range, kCFStringEncodingUTF8, 0, false,
                     reinterpret_cast<UInt8*>(out.data()), used, nullptr);
    return out;
}
} // namespace convolver::au
