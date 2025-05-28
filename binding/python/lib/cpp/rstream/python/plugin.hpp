// See LICENSE file in the project root for license information.

#pragma once

#include "detail/plugin/element.hpp"
#include "detail/plugin/plugin.hpp"

#define RSTREAM_PLUGIN_PLUGIN_CAST(_plugin) \
  std::dynamic_pointer_cast<rstream::python::plugin::plugin>(_plugin)

#define RSTREAM_PLUGIN_PYTHON_STATIC_DECLARE() \
  RSTREAM_PLUGIN_STATIC_DECLARE(python_plugin_wrapper)

#define RSTREAM_PLUGIN_PYTHON_STATIC_REGISTER() \
  RSTREAM_PLUGIN_PLUGIN_CAST(RSTREAM_PLUGIN_STATIC_REGISTER(python_plugin_wrapper))

namespace rstream {
namespace python {
namespace plugin {

using element = detail::plugin::element;
using plugin  = detail::plugin::plugin;

}  // namespace plugin
}  // namespace python
}  // namespace rstream

template <typename T>
struct rstream::core::plugin::dynamic_element_cast_imp<T, rstream::core::detail::plugin::element_cast> {
  std::shared_ptr<T> operator()(const rstream::core::plugin::element::ptr& ptr)
  {
    return rstream::core::plugin::dynamic_element_cast<T, rstream::python::detail::plugin::element_cast>(ptr);
  }
};
