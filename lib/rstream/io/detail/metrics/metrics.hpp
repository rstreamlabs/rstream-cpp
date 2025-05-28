// See LICENSE file in the project root for license information.

#pragma once

#include <cstdint>

namespace rstream {
namespace io {
namespace detail {
namespace metrics {

struct settings_exposer {
  std::uint32_t m_timeouts_start_ms;
};

}  // namespace metrics
}  // namespace detail
}  // namespace io
}  // namespace rstream
