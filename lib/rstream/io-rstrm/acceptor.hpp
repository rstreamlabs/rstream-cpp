// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <rstream/core/allocator.hpp>
#include <rstream/io/acceptor_base.hpp>

#include "endpoint.hpp"
#include "socket.hpp"

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

class acceptor : public io::acceptor_base<endpoint, socket> {
 public:
  acceptor(const io_object::executor_type& executor, core::allocator::ptr allocator = nullptr);

  acceptor(const io_object::executor_type& executor, const settings_acceptor& settings, core::allocator::ptr allocator = nullptr);

  virtual ~acceptor() = default;

  using on_status_cb            = std::function<void(const status_extd&)>;
  using on_tunnel_properties_cb = std::function<void(const tunnel_properties&)>;
  struct control_callbacks {
    on_status_cb m_on_status_cb;
    on_tunnel_properties_cb m_on_tunnel_properties_cb;
  };
  void set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code);

  settings_acceptor settings(boost::system::error_code& error_code) const;

  void settings(const settings_acceptor& settings, boost::system::error_code& error_code);

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  void bind(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void listen(int backlog, boost::system::error_code& error_code) override;

  endpoint local_endpoint(boost::system::error_code& error_code) override;

 private:
  class impl;

  void async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler) override;

  std::shared_ptr<impl> m_impl;
};

}  // namespace io_rstrm
}  // namespace rstream
