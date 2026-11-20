// SPDX-License-Identifier: GPL-2.0-or-later
// ROBUST-8: HostFilter::activation() and HostFilter::activate() in one call,
// for tests that build nothing between them that may fail. Adapters call
// them apart, recording the activation only once its processor will play.
#pragma once
#include "convolver/host.hpp"
#include <utility>

namespace convolver::test {
inline HostPlayback started(HostFilter& filter, const HostLayout& host) {
    auto activation = filter.activation(host);
    auto playback = activation.playback;
    filter.activate(std::move(activation));
    return playback;
}
} // namespace convolver::test
