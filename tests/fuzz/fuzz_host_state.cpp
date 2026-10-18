// SPDX-License-Identifier: GPL-2.0-or-later
// Embedded host state decoding. Accepted state must re-encode to the same bytes.
#include "convolver/host.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
    try {
        const auto state = convolver::decode_host_state(bytes);
        const auto again = convolver::encode_host_state(state);
        if (again.size() != size || std::memcmp(again.data(), data, size) != 0)
            std::abort();
    } catch (const std::invalid_argument&) {
    } catch (const std::length_error&) {
    }
    return 0;
}
