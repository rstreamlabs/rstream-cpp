// See LICENSE file in the project root for license information.

#include "metrics.hpp"

#include "system.hpp"

namespace rstream {
namespace core {
namespace metrics {

registry::ptr default_registry()
{
  return detail::metrics::default_registry();
}

collectable::ptr system_collector()
{
  static registry::ptr system_collector = nullptr;
  if (!system_collector) {
    auto system_info = get_system_info();
    system_collector = std::make_shared<metrics::registry>();
    metrics::info info("system_info", "informations about host system", {}, system_collector);
    info.set({{"sysname", system_info.m_sysname}, {"nodename", system_info.m_nodename}, {"release", system_info.m_release}, {"version", system_info.m_version}, {"machine", system_info.m_machine}});
  }
  return system_collector;
}

}  // namespace metrics
}  // namespace core
}  // namespace rstream
