// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: the windows' recent filter files (portable/recent_files.hpp):
// beside the log, newest first, without repeats, at most ten, and none when
// the log is turned off.
#include "recent_files.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace convolver;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
void set_log_variable(const std::string& value) {
#ifdef _WIN32
    _putenv_s("CONVOLVER_LOG", value.c_str());
#else
    setenv("CONVOLVER_LOG", value.c_str(), 1);
#endif
}
std::string joined(const std::vector<std::string>& files) {
    std::string text;
    for (const auto& file : files)
        text += file + ";";
    return text;
}

void run(const fs::path& dir) {
    // Beside the log, in a folder made when the first file is remembered.
    set_log_variable((dir / "logs" / "convolver.log").string());
    require(recent_files_path() == dir / "logs" / "recent-filters.txt",
            "list file: " + recent_files_path().string());
    require(recent_files().empty(), "a list before anything was loaded");

    require(remember_recent_file("/filters/a.cfg") && remember_recent_file("/filters/b.wav") &&
                remember_recent_file("/filters/a.cfg"),
            "remembering");
    require(recent_files() == std::vector<std::string>{"/filters/a.cfg", "/filters/b.wav"},
            "newest first, no repeats: " + joined(recent_files()));
    // Paths are kept as given, spaces and all, in UTF-8.
    require(remember_recent_file("/Users/jo/Room filters/kök.cfg") &&
                recent_files().front() == "/Users/jo/Room filters/kök.cfg",
            "a path with spaces and accents");
    require(!remember_recent_file("/filters/two\nlines.cfg") && !remember_recent_file(""),
            "a path with a line break, or none, is not kept");

    for (int i = 0; i < 12; ++i)
        require(remember_recent_file("/filters/" + std::to_string(i) + ".cfg"), "remembering");
    const auto files = recent_files();
    require(files.size() == recent_files_kept && files.front() == "/filters/11.cfg" &&
                files.back() == "/filters/2.cfg",
            "ten kept: " + joined(files));
    // Nothing left beside the list from its replacement.
    std::size_t entries = 0;
    for (const auto& entry : fs::directory_iterator(dir / "logs"))
        entries += entry.path().filename() != "convolver.log" ? 1 : 0;
    require(entries == 1, "files beside the list: " + std::to_string(entries));

    // A list edited by hand on Windows still reads.
    {
        std::ofstream out(recent_files_path(), std::ios::binary | std::ios::trunc);
        out << "C:\\Filters\\room.cfg\r\n\r\nC:\\Filters\\room.cfg\r\n";
    }
    require(recent_files() == std::vector<std::string>{"C:\\Filters\\room.cfg"},
            "CRLF list: " + joined(recent_files()));

    require(forget_recent_files() && recent_files().empty(), "Clear Menu");

#ifndef _WIN32 // Setting a variable empty removes it on Windows.
    set_log_variable("");
    require(recent_files_path().empty() && recent_files().empty() &&
                !remember_recent_file("/filters/a.cfg"),
            "a list with the log turned off");
#endif
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-recent-files-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int status = 0;
    try {
        fs::create_directories(dir);
        run(dir);
        std::cout << "recent files checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    return status;
}
