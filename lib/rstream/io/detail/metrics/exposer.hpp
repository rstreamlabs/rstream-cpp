// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/metrics.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "metrics.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace metrics {

class exposer : public io_object {
 public:
  struct config {
    address m_address;
  };

  exposer(const executor_type& executor, const config& config, const settings_exposer& settings);

  ~exposer() noexcept override;

  void add_collectable(core::metrics::collectable::ptr collectable, const std::string& target = "/metrics");

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace metrics
}  // namespace detail
}  // namespace io
}  // namespace rstream
