// See LICENSE file in the project root for license information.

#pragma once

#include <memory>

#include <rstream/config.hpp>

#include "detail/plugin/common.hpp"
#include "detail/plugin/element.hpp"
#include "detail/plugin/factory.hpp"
#include "detail/plugin/plugin.hpp"
#include "detail/plugin/registry.hpp"

#define RSTREAM_DETAIL_CONCAT_IMPL(_left, _right) _left##_right
#define RSTREAM_DETAIL_CONCAT(_left, _right)      RSTREAM_DETAIL_CONCAT_IMPL(_left, _right)

#define RSTREAM_PLUGIN_EXPORT_FULL(_plugin, ...)                                                                      \
  namespace rstream {                                                                                                 \
  namespace core {                                                                                                    \
  namespace plugin {                                                                                                  \
  RSTREAM_GNUC_INTERNAL                                                                                               \
  rstream::core::plugin::plugin::ptr get_plugin()                                                                     \
  {                                                                                                                   \
    boost::system::error_code error_code;                                                                             \
    const auto location = boost::dll::this_line_location(error_code);                                                 \
    return std::make_shared<_plugin>(error_code ? rstream::core::plugin::plugin::location() : location, __VA_ARGS__); \
  }                                                                                                                   \
  }                                                                                                                   \
  }                                                                                                                   \
  }                                                                                                                   \
  BOOST_DLL_ALIAS(rstream::core::plugin::get_plugin, RSTREAM_PLUGIN_SYMBOL);

#define RSTREAM_PLUGIN_EXPORT(...) \
  RSTREAM_PLUGIN_EXPORT_FULL(rstream::core::detail::plugin::plugin_simple, __VA_ARGS__)

#define RSTREAM_PLUGIN_STATIC_DECLARE(_name)                      \
  namespace rstream {                                             \
  namespace core {                                                \
  namespace plugin {                                              \
  extern rstream::core::plugin::plugin::ptr get_plugin_##_name(); \
  }                                                               \
  }                                                               \
  }

#define RSTREAM_PLUGIN_STATIC_REGISTER(_name) \
  rstream::core::plugin::get_plugin_##_name()

#define RSTREAM_PLUGIN_STATIC_DEFINE_FULL(_plugin, _name, ...)                                                                                                       \
  namespace rstream {                                                                                                                                                \
  namespace core {                                                                                                                                                   \
  namespace plugin {                                                                                                                                                 \
  rstream::core::plugin::plugin::ptr get_plugin_##_name()                                                                                                            \
  {                                                                                                                                                                  \
    boost::system::error_code error_code;                                                                                                                            \
    const auto location = boost::dll::this_line_location(error_code);                                                                                                \
    return std::make_shared<_plugin>(error_code ? rstream::core::plugin::plugin::location() : location, __VA_ARGS__);                                                \
  }                                                                                                                                                                  \
  namespace {                                                                                                                                                        \
  const bool RSTREAM_DETAIL_CONCAT(g_registered_plugin_, _name) = rstream::core::detail::plugin::register_static_plugin(&rstream::core::plugin::get_plugin_##_name); \
  }                                                                                                                                                                  \
  }                                                                                                                                                                  \
  }                                                                                                                                                                  \
  }

#define RSTREAM_PLUGIN_STATIC_DEFINE(...) \
  RSTREAM_PLUGIN_STATIC_DEFINE_FULL(rstream::core::detail::plugin::plugin_simple, __VA_ARGS__)

namespace rstream {
namespace core {
namespace plugin {
using config  = detail::plugin::config;
using element = detail::plugin::element;
using factory = detail::plugin::factory;
using plugin  = detail::plugin::plugin;
template <class T, class... Args>
element::handle make_element(Args&&... args)
{
  element::handle handle = {
      .m_info        = T::get_element_info(),
      .m_create_func = [args...]() {
        return std::make_shared<T>(args...);
      },
  };
  return handle;
}
detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info);
detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info, const detail::plugin::elements& elements);
detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info, const std::list<element::handle>& elements);
template <typename T, typename P>
struct dynamic_element_cast_imp {
  std::shared_ptr<T> operator()(const element::ptr& ptr)
  {
    return P().template operator()<T>(ptr);
  }
};
template <typename T, typename P = detail::plugin::element_cast>
std::shared_ptr<T> dynamic_element_cast(const element::ptr& ptr)
{
  return dynamic_element_cast_imp<T, P>()(ptr);
}
}  // namespace plugin
}  // namespace core
}  // namespace rstream
