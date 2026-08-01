// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include "metric.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class collectable {
 public:
  using ptr                              = std::shared_ptr<collectable>;
  virtual ~collectable()                 = default;
  virtual void collect(metrics& metrics) = 0;
  metrics collect();
};

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
