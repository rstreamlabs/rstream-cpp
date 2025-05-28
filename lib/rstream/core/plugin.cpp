// See LICENSE file in the project root for license information.

#include "plugin.hpp"

namespace rstream {
namespace core {
namespace plugin {

detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info)
{
  return detail::plugin::plugin_simple::descriptor(info, {});
}

detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info, const detail::plugin::elements& elements)
{
  return detail::plugin::plugin_simple::descriptor(info, elements);
}

detail::plugin::plugin_simple::descriptor make_plugin_descriptor(const plugin::info& info, const std::list<element::handle>& elements)
{
  detail::plugin::elements tmp;
  for (const auto& element : elements) {
    tmp.insert(std::make_pair(element.m_info.m_name, element));
  }
  return make_plugin_descriptor(info, tmp);
}

}  // namespace plugin
}  // namespace core
}  // namespace rstream
