// See LICENSE file in the project root for license information.

#pragma once

#include <list>
#include <map>
#include <shared_mutex>

#include "collectable.hpp"

namespace rstream {
namespace core {
namespace detail {
namespace metrics {

class wrapper_common;

class registry : public collectable {
  friend class wrapper_common;

 public:
  using ptr  = std::shared_ptr<registry>;
  registry() = default;
  virtual ~registry();
  void collect(metrics& metrics) override;
  void add_collectable(collectable::ptr collectable);

 private:
  using metric = std::shared_ptr<wrapper_common>;
  void add_metric(metric metric);
  std::shared_mutex m_mutex;
  std::list<collectable::ptr> m_collectables;
  std::map<std::string, metric> m_metrics;
};

registry::ptr default_registry();

}  // namespace metrics
}  // namespace detail
}  // namespace core
}  // namespace rstream
