// SPDX-License-Identifier: GPL-2.0-or-later
// Private: how the convolver command line reads a path that may name a pipe
// or a descriptor (IO-16, IO-17), shared by rendering and --inspect.
#pragma once
#include <filesystem>
#include <optional>

namespace convolver::cli {
// Whether a path names a pipe, socket or device rather than a file (the
// shell's <(command), /dev/stdin fed by a pipe, a named pipe), which can be
// read only once, from the start. Always false on Windows.
bool is_stream(const std::filesystem::path&);
#ifndef _WIN32
// The descriptor that /dev/stdin, /dev/stdout, /dev/stderr or /dev/fd/N names.
std::optional<int> named_descriptor(const std::filesystem::path&);
#endif
// The path to read a file by: for /dev/stdin or /dev/fd/N open on a regular
// file, that file's own path; otherwise the path itself.
std::filesystem::path readable_path(const std::filesystem::path&);
} // namespace convolver::cli
