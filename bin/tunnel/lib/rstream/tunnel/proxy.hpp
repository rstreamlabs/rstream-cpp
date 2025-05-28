// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "tunnel.hpp"

namespace rstream {
namespace tunnel {

class proxy : public io::io_object {
 public:
  struct config {
    io_rstrm::endpoint m_local_endpoint;
    io::address m_target_address;
    io_rstrm::settings_acceptor m_settings_acceptor;
  };

  struct callbacks {
    on_status_cb m_on_status_cb;
    on_new_connection_cb m_on_new_connection_cb;
  };

  proxy(const executor_type& executor, const config& config, const settings_proxy& settings);

  virtual ~proxy();

  using async_run_completion_handler = core::completion_handler<void(const std::error_code&)>;

  void async_run(const callbacks& callbacks, async_run_completion_handler&& handler);

  void cancel();

 private:
  class impl;

  std::shared_ptr<impl> m_impl;
};

}  // namespace tunnel
}  // namespace rstream
