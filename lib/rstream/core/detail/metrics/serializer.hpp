// See LICENSE file in the project root for license information.

#pragma once

#include <string>

#include "metric.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class serializer {
 public:
  std::string operator()(const metrics& metrics) const;
};

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
