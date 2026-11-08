// SPDX-License-Identifier: GPL-2.0-or-later
#include "partitions.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace convolver::detail {
namespace {
const PartitionLayout default_layout{};
const PartitionLayout* forced_layout = nullptr;
} // namespace
const PartitionLayout& partition_layout() noexcept {
    return forced_layout ? *forced_layout : default_layout;
}
void force_partition_layout(const PartitionLayout* layout) noexcept {
    forced_layout = layout;
}
std::size_t Partitioning::partitions(std::size_t s, std::size_t taps) const noexcept {
    const auto& stage = stages[s];
    const auto end = taps + planned; // Output time after the filter's last tap.
    if (end <= stage.offset)
        return 0;
    return std::min(stage.partitions, (end - stage.offset + stage.block - 1) / stage.block);
}
std::size_t Partitioning::head_taps(std::size_t taps) const noexcept {
    return std::min(taps, start - planned);
}
namespace {
// Floating-point operations per output frame of one stage, for one input mix
// and one path: a forward and an inverse real transform of 2n points (about
// 5 n log2(2n) each) and a complex multiply-add per bin and partition (8 per
// bin), every n frames. Only the comparison between layouts matters.
double stage_cost(std::size_t n, std::size_t partitions) {
    const auto size = static_cast<double>(n);
    return (10.0 * size * std::log2(2.0 * size) +
            8.0 * static_cast<double>(partitions) * (size + 1)) /
           size;
}
struct Plan {
    double cost = 0;
    std::vector<Stage> stages;
};
// The cheapest stages covering output times [time, end), the first of block
// at least `smallest`: each stage either covers the rest, or stops where the
// next size becomes possible. Run once per filter load, never while
// processing.
class Planner {
  public:
    // Blocks up to `whole` frames may be computed whole when they complete.
    Planner(const PartitionLayout& layout, std::size_t end, std::size_t whole)
        : layout_(layout), end_(end), whole_(std::max(whole, layout.first)) {}
    Plan best(std::size_t time, std::size_t smallest) {
        const auto key = std::pair{time, smallest};
        if (const auto found = memo_.find(key); found != memo_.end())
            return found->second;
        Plan result;
        result.cost = std::numeric_limits<double>::infinity();
        for (std::size_t n = smallest;; n = grow(n)) {
            // Larger blocks than the first spread their work over a block
            // period of slack when they have it; blocks of the first size,
            // and others no larger than the latency, are computed whole when
            // they complete, which needs no slack.
            const bool spread = n != layout_.first && time >= 2 * n;
            if (!spread && time < whole_start(n))
                break;
            // This size for all the rest.
            consider(result, {}, n, time, end_, !spread);
            // This size until the next one is possible, then the best rest.
            if (n < layout_.largest) {
                const auto next = grow(n);
                const auto partitions =
                    (std::max(time + n, std::min(2 * next, whole_start(next))) - time + n - 1) / n;
                if (time + partitions * n < end_)
                    consider(result, best(time + partitions * n, next), n, time,
                             time + partitions * n, !spread);
            }
            if (n == layout_.largest)
                break;
        }
        memo_[key] = result;
        return result;
    }

  private:
    std::size_t grow(std::size_t n) const {
        return std::min(n * layout_.growth, layout_.largest);
    }
    // The earliest output time at which a block of n can be computed whole:
    // its offset must be at least the block. Never, above the limit.
    std::size_t whole_start(std::size_t n) const {
        return n <= whole_ ? n : std::numeric_limits<std::size_t>::max();
    }
    static void consider(Plan& result, const Plan& rest, std::size_t n, std::size_t time,
                         std::size_t stop, bool whole) {
        const auto partitions = (stop - time + n - 1) / n;
        const auto cost = stage_cost(n, partitions) + rest.cost;
        if (cost >= result.cost)
            return;
        result.cost = cost;
        result.stages.assign(1, Stage{n, time, partitions, whole});
        result.stages.insert(result.stages.end(), rest.stages.begin(), rest.stages.end());
    }
    const PartitionLayout& layout_;
    std::size_t end_, whole_;
    std::map<std::pair<std::size_t, std::size_t>, Plan> memo_;
};
} // namespace
Partitioning partition(std::size_t taps, std::size_t latency, const PartitionLayout& layout) {
    Partitioning result;
    result.planned = std::min(latency, 2 * layout.largest);
    result.extra = latency - result.planned;
    // Output time t carries tap t - planned. The direct sum covers the taps
    // whose output time comes before the first block can be ready.
    result.start = std::max(result.planned, layout.first);
    result.head = result.head_taps(taps);
    const auto end = taps + result.planned;
    if (result.start < end)
        result.stages =
            Planner(layout, end, result.planned).best(result.start, layout.first).stages;
    // The direct sum's results land up to planned + first frames ahead; a
    // block's results at most offset + n frames beyond the frame being
    // produced when they are added.
    std::size_t reach = result.start + layout.first, largest = layout.first;
    for (const auto& stage : result.stages) {
        reach = std::max(reach, stage.offset + stage.block);
        largest = std::max(largest, stage.block);
    }
    result.output_ring = std::bit_ceil(reach);
    // A block is read from the mixed input up to a block period after it
    // completes, while the next block is written.
    result.input_ring = std::bit_ceil(2 * largest);
    return result;
}
} // namespace convolver::detail
