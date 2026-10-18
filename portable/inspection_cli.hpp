// SPDX-License-Identifier: GPL-2.0-or-later
// Private: --inspect / --validate command line, human and JSON report output.
#pragma once
#include "convolver/inspection.hpp"
#include <iosfwd>
#include <string>

namespace convolver::cli {
// True when argv selects an inspection mode, which run_inspection then handles.
bool is_inspection(int argc, char** argv);
// Exit status: 0 valid, 1 invalid/I-O/internal failure, 2 invalid usage.
int run_inspection(int argc, char** argv);
// Report schema 1 as one JSON document (UTF-8, no trailing newline).
std::string report_json(const InspectionReport&, bool validate, bool warnings_as_errors);
} // namespace convolver::cli
