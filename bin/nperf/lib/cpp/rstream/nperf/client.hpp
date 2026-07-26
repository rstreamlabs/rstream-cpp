// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>
#include <boost/variant.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "nperf.hpp"

namespace rstream {
namespace nperf {

class client : public io::io_object {
 public:
  struct config {
    io::address m_address;
  };

  struct callbacks {
    on_metrics_cb m_on_metrics_cb;
  };

  client(const executor_type& executor, const config& config, const settings_client& settings);

  ~client() noexcept override;

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace nperf
}  // namespace rstream
