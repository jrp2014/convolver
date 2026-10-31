// SPDX-License-Identifier: GPL-2.0-or-later
// Impulse-response decoding through libsndfile and the production decoder. The
// input is written to one harness-owned file in a private temporary directory;
// configuration text is not followed, so no other file is opened.
#include "config_parse.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>

namespace {
struct Scratch {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("convolver-fuzz-ir-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(dir);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
const std::filesystem::path& scratch() {
    static const Scratch owned;
    static const auto path = owned.dir / "input.wav";
    return path;
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    {
        std::ofstream file(scratch(), std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!file)
            return 0;
    }
    try {
        const auto reader = convolver::detail::libsndfile_files().open(scratch());
        std::size_t budget = 0;
        const auto cfg = convolver::detail::decode_audio_filter(*reader, budget, {scratch(), 0, 0});
        if (cfg.paths.size() != reader->info().channels ||
            budget > convolver::detail::max_decoded_samples)
            std::abort();
    } catch (const std::runtime_error&) {
        // Includes ConfigurationError and libsndfile/WAV validation failures.
    }
    return 0;
}
