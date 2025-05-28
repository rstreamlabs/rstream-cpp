// See LICENSE file in the project root for license information.

#pragma once

#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class counter : public wrapper<counter> {
  friend class wrapper<counter>;

 public:
  counter(const std::string& name, const std::string& help, const detail::metrics::labels& labels = {}, registry::ptr registry = nullptr);
  void increment(const examplar& examplar = {});
  void increment(double value, const examplar& examplar = {});
  void decrement(const examplar& examplar = {});
  void decrement(double value, const examplar& examplar = {});
  double value() const;

 private:
  counter(wrapper_common::ptr parent, const detail::metrics::labels& labels);
  counter(std::shared_ptr<impl> impl);
};

template <>
std::shared_ptr<wrapper_base<counter>> wrapper<counter>::get_base() const;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
