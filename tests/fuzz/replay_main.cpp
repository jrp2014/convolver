// SPDX-License-Identifier: GPL-2.0-or-later
// Deterministic replay of fuzz seeds and minimized regressions on every platform,
// without libFuzzer: each argument is a file or a directory of files.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

int main(int argc, char** argv) {
    std::size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        std::vector<std::filesystem::path> inputs;
        if (std::filesystem::is_directory(argv[i])) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(argv[i]))
                if (e.is_regular_file())
                    inputs.push_back(e.path());
        } else
            inputs.emplace_back(argv[i]);
        for (const auto& input : inputs) {
            std::ifstream file(input, std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
            LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                   bytes.size());
            ++count;
        }
    }
    if (!count) {
        std::cerr << "no fuzz inputs found\n";
        return 1;
    }
    std::cout << "replayed " << count << " fuzz inputs\n";
}
