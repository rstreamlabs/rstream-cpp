// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <map>
#include <string>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

using timestamp = std::chrono::time_point<std::chrono::system_clock>;

using labels = std::map<std::string, std::string>;

using examplar = std::map<std::string, std::string>;

void check_metric_name(const std::string& name);

void check_label_name(const std::string& name);

void check_label_name(const detail::metrics::labels& labels);

void check_label_name_overlap(const labels& la, const labels& lb);

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
