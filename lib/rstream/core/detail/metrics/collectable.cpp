// See LICENSE file in the project root for license information.

#include "collectable.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

metrics collectable::collect()
{
  metrics metrics = {};
  collect(metrics);
  return metrics;
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
