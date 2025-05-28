// See LICENSE file in the project root for license information.

#pragma once

#include "detail/metrics/exposer.hpp"
#include "detail/metrics/metrics.hpp"

namespace rstream {
namespace io {
class metrics {
 public:
  using settings_exposer = detail::metrics::settings_exposer;
  using exposer          = detail::metrics::exposer;
};

}  // namespace io
}  // namespace rstream
