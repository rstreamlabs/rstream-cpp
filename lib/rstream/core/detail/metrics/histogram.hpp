// See LICENSE file in the project root for license information.

#pragma once

#include <vector>

#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class histogram : public wrapper<histogram> {
  friend class wrapper<histogram>;

 public:
  using upper_boundaries = std::vector<double>;
  histogram(const std::string& name, const std::string& help, const detail::metrics::labels& labels = {}, registry::ptr registry = nullptr, const upper_boundaries& upper_boundaries = default_upper_boundaries());
  void observe(double value, const examplar& examplar = {});
  static const upper_boundaries& default_upper_boundaries();

 private:
  histogram(wrapper_common::ptr parent, const detail::metrics::labels& labels, const upper_boundaries& upper_boundaries);
  histogram(std::shared_ptr<impl> impl);
};

template <>
std::shared_ptr<wrapper_base<histogram>> wrapper<histogram>::get_base() const;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
