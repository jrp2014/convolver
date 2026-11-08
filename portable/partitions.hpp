// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <vector>

namespace convolver::detail {
// PERF-2: how the engine divides a filter. The first taps, up to the first
// block's output time, are summed directly, which gives zero latency. The
// rest is covered by stages of uniform blocks that grow by `growth` from
// `first` to `largest` frames; each stage is a uniformly partitioned
// overlap-add convolution whose output starts at `offset` frames after its
// input block. Blocks of `first` frames are computed when they complete;
// larger blocks' work is spread over the callbacks of the following block
// period, which the layout leaves as slack (an offset of at least twice the
// block). Measured on 7 November 2026: docs/develop/fft-measurements.md.
struct PartitionLayout {
    std::size_t first = 64, largest = 8192, growth = 4;
};
const PartitionLayout& partition_layout() noexcept;
// Tests only: smaller blocks, so short filters exercise every stage and
// boundary. Powers of two, first <= largest, growth 2 or 4; nullptr restores
// the default. Not thread-safe: call before constructing engines.
void force_partition_layout(const PartitionLayout* layout) noexcept;

struct Stage {
    std::size_t block = 0, offset = 0, partitions = 0;
};
struct Partitioning {
    // The latency the stages use, at most twice the largest block; any more
    // is a plain delay on the outputs (`extra`), since a later start saves
    // nothing beyond it and would only enlarge the output buffers.
    std::size_t planned = 0, extra = 0;
    std::size_t start = 0;       // Output time of the first stage.
    std::size_t head = 0;        // Directly summed taps of the longest filter.
    std::size_t output_ring = 0; // Frames buffered per output, a power of two.
    std::size_t input_ring = 0;  // Mixed input frames kept per input mix.
    std::vector<Stage> stages;
    // Partitions of stage s that a filter of `taps` taps uses.
    std::size_t partitions(std::size_t s, std::size_t taps) const noexcept;
    std::size_t head_taps(std::size_t taps) const noexcept;
};
Partitioning partition(std::size_t longest_taps, std::size_t latency,
                       const PartitionLayout& = partition_layout());
} // namespace convolver::detail
