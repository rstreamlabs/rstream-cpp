// See LICENSE file in the project root for license information.

#include "common.hpp"

#include <regex>
#include <stdexcept>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

void check_metric_name(const std::string& name)
{
  static const std::regex regex("[a-zA-Z_:][a-zA-Z0-9_:]*", std::regex_constants::ECMAScript);
  if (!std::regex_match(name, regex)) {
    throw std::invalid_argument("invalid metric name '" + name + "'");
  }
}

void check_label_name(const std::string& name)
{
  static const std::regex regex("[a-zA-Z_][a-zA-Z0-9_]*", std::regex_constants::ECMAScript);
  if (!std::regex_match(name, regex)) {
    throw std::invalid_argument("invalid label name '" + name + "'");
  }
}

void check_label_name(const detail::metrics::labels& labels)
{
  for (const auto& label : labels) {
    check_label_name(label.first);
  }
}

void check_label_name_overlap(const labels& la, const labels& lb)
{
  for (const auto& label : la) {
    if (lb.find(label.first) != lb.end()) {
      throw std::invalid_argument("duplicate label name '" + label.first + "'");
    }
  }
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
