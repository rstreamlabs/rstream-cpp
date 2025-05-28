// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

#include "quantile.hpp"
#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class summary : public wrapper<summary> {
  friend class wrapper<summary>;

 public:
  summary(const std::string& name, const std::string& help, const detail::metrics::labels& labels = {}, registry::ptr registry = nullptr, const quantiles& quantiles = default_quantiles(), const std::chrono::milliseconds& time_window_s = std::chrono::seconds(10 * 60), const std::size_t& age_buckets = 5);
  void observe(double value, const examplar& examplar = {});
  static quantiles default_quantiles();

 private:
  summary(wrapper_common::ptr parent, const detail::metrics::labels& labels, const quantiles& quantiles, const std::chrono::milliseconds& time_window_s, const std::size_t& age_buckets);
  summary(std::shared_ptr<impl> impl);
};

template <>
std::shared_ptr<wrapper_base<summary>> wrapper<summary>::get_base() const;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
