// See LICENSE file in the project root for license information.

#pragma once

#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class gauge : public wrapper<gauge> {
  friend class wrapper<gauge>;

 public:
  gauge(const std::string& name, const std::string& help, const detail::metrics::labels& labels = {}, registry::ptr registry = nullptr);
  void increment(const examplar& examplar = {});
  void increment(double value, const examplar& examplar = {});
  void decrement(const examplar& examplar = {});
  void decrement(double value, const examplar& examplar = {});
  void set(double value, const examplar& examplar = {});
  void set_current_time(const examplar& examplar = {});
  double value() const;

 private:
  gauge(wrapper_common::ptr parent, const detail::metrics::labels& labels);
  gauge(std::shared_ptr<impl> impl);
};

template <>
std::shared_ptr<wrapper_base<gauge>> wrapper<gauge>::get_base() const;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
