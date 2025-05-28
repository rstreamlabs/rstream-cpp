// See LICENSE file in the project root for license information.

#include "registry.hpp"

#include <exception>

#include "wrapper.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

static registry::ptr g_default_registry = std::make_shared<registry>();

registry::~registry()
{
  std::shared_lock lock(m_mutex);
  for (const auto& metric : m_metrics) {
    metric.second->deinit();
  }
}

void registry::collect(metrics& metrics)
{
  std::shared_lock lock(m_mutex);
  for (const auto& metric : m_metrics) {
    metrics.push_back(metric.second->get_metric());
  }
  for (const auto& collectable : m_collectables) {
    collectable->collect(metrics);
  }
}

void registry::add_collectable(collectable::ptr collectable)
{
  std::unique_lock lock(m_mutex);
  m_collectables.push_back(collectable);
}

void registry::add_metric(metric metric)
{
  std::unique_lock lock(m_mutex);
  m_metrics.insert(std::make_pair(metric->name(), metric));
}

registry::ptr default_registry()
{
  return g_default_registry;
}

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
