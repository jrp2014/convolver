// SPDX-License-Identifier: GPL-2.0-or-later
// Core Foundation helpers shared by the Audio Unit's implementation files.
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <string>

namespace convolver::au {
// UTF-8 text as a CFString the caller releases; null if out of memory.
inline CFStringRef cf_string(const std::string& text) {
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(text.data()),
                                   static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
}
} // namespace convolver::au
