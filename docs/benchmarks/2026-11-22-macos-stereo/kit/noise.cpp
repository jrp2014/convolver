// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <boost/random/lagged_fibonacci.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <cstddef>
extern "C" void archival_noise(float* samples, size_t count) {
    // Same generator/distribution as the original, with a fixed, reproducible seed.
    typedef boost::lagged_fibonacci607 generator_type;
    generator_type generator(9817u);
    boost::uniform_real<float> distribution(-1.0f, 1.0f);
    boost::variate_generator<generator_type&, boost::uniform_real<float>> uniform(generator,
                                                                                  distribution);
    std::generate_n(samples, count, uniform);
}
