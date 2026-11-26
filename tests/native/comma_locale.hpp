// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-55 (jrp's review of 7bd024f): a plug-in shares its host's C locale, so
// text Convolver writes for people must not change with LC_NUMERIC. While one
// of these exists, LC_NUMERIC is a locale with a decimal comma, when this
// machine has one (macOS and Windows do; a Linux machine needs one generated,
// as `localedef -i de_DE -f UTF-8 de_DE.UTF-8` does).
#pragma once
#include <clocale>
#include <cstdio>
#include <string>

namespace convolver::test {
class CommaLocale {
  public:
    CommaLocale() {
        if (const char* now = std::setlocale(LC_NUMERIC, nullptr))
            before_ = now;
        for (const char* name : {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR",
                                 "German_Germany.1252", "de-DE"}) {
            if (!std::setlocale(LC_NUMERIC, name))
                continue;
            char text[8] = {};
            std::snprintf(text, sizeof text, "%.1f", 1.5);
            if (std::string(text) == "1,5") {
                active_ = name;
                return;
            }
        }
        std::setlocale(LC_NUMERIC, before_.c_str());
    }
    ~CommaLocale() {
        std::setlocale(LC_NUMERIC, before_.c_str());
    }
    CommaLocale(const CommaLocale&) = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;
    // The locale in use, or empty when this machine has none with a comma.
    const std::string& active() const noexcept {
        return active_;
    }

  private:
    std::string before_ = "C", active_;
};
// Runs `check` in a comma locale and says which, or that there was none.
template <class Check> void in_comma_locale(const char* what, Check check) {
    const CommaLocale comma;
    std::printf("%s: %s\n", what,
                comma.active().empty() ? "no locale with a decimal comma here; checked in C"
                                       : ("checked in " + comma.active()).c_str());
    check();
}
} // namespace convolver::test
