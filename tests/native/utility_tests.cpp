// SPDX-License-Identifier: GPL-2.0-or-later
#include "bytes.hpp"
#include "file.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <stdexcept>
#include <thread>
using namespace convolver::detail;
namespace fs = std::filesystem;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F>
void rejects(F f, const std::source_location where = std::source_location::current()) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("operation unexpectedly succeeded at line " +
                             std::to_string(where.line()));
}
std::string contents(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "read output fixture");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void bytes_test() {
    const std::array<std::byte, 6> bytes{std::byte{0xaa}, std::byte{0xff}, std::byte{0x80},
                                         std::byte{0x01}, std::byte{0xfe}, std::byte{0xbb}};
    const auto unaligned = std::span(bytes).subspan(1);
    require(read_le16(unaligned) == 0x80ff, "little-endian 16-bit high bits");
    require(read_le32(unaligned) == 0xfe0180ff, "unaligned little-endian 32-bit high bits");
    auto out = bytes;
    write_le32(std::span(out).subspan(1), 0x12345678);
    const std::array<std::byte, 6> expected{std::byte{0xaa}, std::byte{0x78}, std::byte{0x56},
                                            std::byte{0x34}, std::byte{0x12}, std::byte{0xbb}};
    require(out == expected, "little-endian write preserves surrounding bytes");
    for (std::size_t n = 0; n < 4; ++n) {
        rejects([&] { read_le32(std::span(bytes).first(n)); });
        if (n < 2)
            rejects([&] { read_le16(std::span(bytes).first(n)); });
        const auto before = out;
        rejects([&] { write_le32(std::span(out).first(n), 0xffffffff); });
        require(out == before, "short writes must not modify the destination");
    }
}
void files_test(const fs::path& dir) {
    const auto path = dir / fs::path(u8"binary ü.dat");
    const std::string data("\0\xff\r\n\x80", 5);
    const auto bytes = std::as_bytes(std::span(data));
    write_new_file(path, bytes);
    require(contents(path) == data, "binary and Unicode output round trip");
    rejects([&] { write_new_file(path, {}); });
    require(contents(path) == data, "existing output must not be truncated");
    const auto hard = dir / "hard-link";
    fs::create_hard_link(path, hard);
    rejects([&] { write_new_file(hard, {}); });
    require(contents(path) == data, "hard-linked output must not be truncated");
    std::error_code ec;
    const auto link = dir / "link";
    fs::create_symlink(path, link, ec);
    if (!ec) {
        rejects([&] { write_new_file(link, {}); });
        require(contents(path) == data, "symlink target must not be truncated");
        fs::remove(link);
        const auto missing = dir / "missing-target";
        fs::create_symlink(missing, link);
        rejects([&] { write_new_file(link, bytes); });
        require(!fs::exists(missing), "dangling symlink target must not be created");
    } else {
#ifdef _WIN32
        std::cout << "Symlink checks unavailable: " << ec.message() << '\n';
#else
        throw fs::filesystem_error("create symlink fixture", link, ec);
#endif
    }
    const auto empty = dir / "empty";
    write_new_file(empty, {});
    require(fs::is_regular_file(empty) && fs::file_size(empty) == 0, "empty file creation");
    rejects([&] { write_new_file(dir / "absent" / "file", bytes); });
    rejects([&] { write_new_file(dir, bytes); });
    const auto race = dir / "race";
    std::array<bool, 2> created{};
    auto attempt = [&](std::size_t i) {
        try {
            write_new_file(race, bytes);
            created[i] = true;
        } catch (const std::exception&) {
        }
    };
    std::thread first(attempt, 0), second(attempt, 1);
    first.join();
    second.join();
    require(created[0] != created[1] && contents(race) == data,
            "exactly one concurrent exclusive creator must succeed");
}
int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-utilities-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        bytes_test();
        require(fs::create_directory(dir), "create temporary directory");
        files_test(dir);
        fs::remove_all(dir);
        std::cout << "byte encoding and exclusive file creation passed\n";
    } catch (const std::exception& e) {
        fs::remove_all(dir);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
