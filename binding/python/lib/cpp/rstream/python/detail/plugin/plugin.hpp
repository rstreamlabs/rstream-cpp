// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/core/plugin.hpp>
#include <rstream/python/python.hpp>

namespace rstream {
namespace python {
namespace detail {
namespace plugin {

class plugin : public rstream::core::plugin::plugin {
 public:
  plugin(const rstream::core::plugin::plugin::location& location, const rstream::core::plugin::plugin::info& info);
  virtual ~plugin();
  void register_module(const std::string& filename);
  const rstream::core::detail::plugin::elements& get_elements() override;

 private:
  void init() override;
  void register_module_internal(const std::string& filename);
  static rstream::core::plugin::config get_default_config(const rstream::core::plugin::config& base);
  rstream::core::plugin::config m_config;
  rstream::core::detail::plugin::elements m_elements;
};

}  // namespace plugin
}  // namespace detail
}  // namespace python
}  // namespace rstream
