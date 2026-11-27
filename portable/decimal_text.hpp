// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-55 (jrp's review of 7bd024f): numbers written for people and read back,
// always with a decimal point. printf and strtod follow the process's
// LC_NUMERIC, which a plug-in shares with its host: a host running in a
// German or French locale made "-6.0" read "-6,0", left its trailing zero in
// place and turned "-0.0" into "-0,0", and a window parsing such text back
// placed the dimmed Gain slider at the wrong value. These use the classic
// locale whatever the host chose. Not for the audio thread (they allocate).
#pragma once
#include <ios>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace convolver::detail {
// As printf's "%.*f" (and "%+.*f" with `plus`) in the C locale.
inline std::string fixed_text(double value, int places, bool plus = false) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (plus)
        out << std::showpos;
    out << std::fixed;
    out.precision(places);
    out << value;
    return out.str();
}
// As printf's "%.*g" (and "%+.*g" with `plus`) in the C locale.
inline std::string general_text(double value, int significant, bool plus = false) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (plus)
        out << std::showpos;
    out.precision(significant);
    out << value;
    return out.str();
}
// A number written by fixed_text() or general_text(), read the same way; none
// for text that does not start with one.
inline std::optional<double> decimal_value(std::string_view text) {
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    double value = 0;
    in >> value;
    return in.fail() ? std::nullopt : std::optional(value);
}
} // namespace convolver::detail
