// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>
#include <vector>

#include <rstream/nperf/nperf.hpp>

namespace rstream {
namespace nperf {
namespace detail {

sample compute_sample(const std::vector<std::uint64_t>& values, sample::type type);

}  // namespace detail
}  // namespace nperf
}  // namespace rstream
