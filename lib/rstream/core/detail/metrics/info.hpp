// See LICENSE file in the project root for license information.

#pragma once

#include <map>
#include <string>

#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class info : public wrapper<info> {
  friend class wrapper<info>;

 public:
  using value_type = std::map<std::string, std::string>;
  info(const std::string& name, const std::string& help, const detail::metrics::labels& labels = {}, registry::ptr registry = nullptr);
  void set(const value_type& value, const examplar& examplar = {});

 private:
  info(wrapper_common::ptr parent, const detail::metrics::labels& labels);
  info(std::shared_ptr<impl> impl);
};

template <>
std::shared_ptr<wrapper_base<info>> wrapper<info>::get_base() const;

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
