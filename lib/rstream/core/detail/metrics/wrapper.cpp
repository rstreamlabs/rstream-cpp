// See LICENSE file in the project root for license information.

#include "wrapper.hpp"

#include <exception>

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

wrapper_common::description::description(metric::type type, const std::string& name, const std::string& help)
    : m_type(type),
      m_name(name),
      m_help(help)
{
}

sample wrapper_common::get_sample()
{
  throw std::logic_error(std::string("unexpected call hierarchy for function '") + __PRETTY_FUNCTION__ + "'");
}

labels wrapper_common::get_labels(bool recursive) const
{
  labels labels;
  get_labels(labels, recursive);
  return labels;
}

void wrapper_common::register_metric(registry::ptr registry, ptr metric)
{
  auto ptr = registry ? registry : default_registry();
  ptr->add_metric(metric);
}

void wrapper_common::deinit()
{
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
