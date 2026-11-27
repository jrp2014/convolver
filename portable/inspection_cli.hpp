// SPDX-License-Identifier: GPL-2.0-or-later
// Private: --inspect / --validate command line, human and JSON report output.
#pragma once
#include "convolver/inspection.hpp"
#include <filesystem>
#include <iosfwd>
#include <string>

namespace convolver::cli {
// True when argv selects an inspection mode, which run_inspection then handles.
bool is_inspection(int argc, char** argv);
// Exit status: 0 valid, 1 invalid/I-O/internal failure, 2 invalid usage.
int run_inspection(int argc, char** argv);
// Report schema 1 as one JSON document (UTF-8, no trailing newline).
// Adds an invalid-utf8 warning to the report if any serialized text is replaced.
std::string report_json(InspectionReport&, bool validate, bool warnings_as_errors);
// IO-17: whether a path names a pipe, socket or device rather than a file
// (the shell's <(command), /dev/stdin fed by a pipe, a named pipe), which can
// be read only once, from the start. Always false on Windows.
bool is_stream(const std::filesystem::path&);
} // namespace convolver::cli
