// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include <boost/asio/async_result.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/address.hpp>
#include <rstream/io/io_object.hpp>

#include "tunnel.hpp"

#include "io-rstrm.hpp"

namespace rstream {
namespace io_rstrm {

class client : public io::io_object {
  friend class tunnel::impl;

 public:
  client(const executor_type& executor, const config_client& config, core::allocator::ptr allocator = nullptr);

  client(const executor_type& executor, core::allocator::ptr allocator = nullptr);

  virtual ~client() = default;

  // get the address of the rstream engine
  io::address address(boost::system::error_code& error_code) const;

  // set control callbacks
  using on_disconnection_cb = std::function<void(const boost::system::error_code&)>;
  using on_status_cb        = std::function<void(const status&)>;
  struct control_callbacks {
    on_disconnection_cb m_on_disconnection_cb;
    on_status_cb m_on_status_cb;
  };
  void set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code);

  // open connection with rstream engine
  template <typename connect_handler>
  auto async_connect(const io::address& address, BOOST_ASIO_MOVE_ARG(connect_handler) handler);

  template <typename connect_handler>
  auto async_connect(BOOST_ASIO_MOVE_ARG(connect_handler) handler);

  // create a tunnel
  template <typename create_tunnel_handler>
  auto async_create_tunnel(const tunnel_properties& properties, BOOST_ASIO_MOVE_ARG(create_tunnel_handler) handler);

  // close connection with rstream engine
  void close();

 private:
  class impl;

  using async_connect_completion_handler = core::completion_handler<void(const boost::system::error_code&)>;
  void async_connect_internal(const io::address& address, async_connect_completion_handler&& handler);
  void async_connect_internal(async_connect_completion_handler&& handler);

  using async_create_tunnel_completion_handler = core::completion_handler<void(const boost::system::error_code&, tunnel)>;
  void async_create_tunnel_internal(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler);

  // implementation details
  std::shared_ptr<impl> m_impl;
};

template <typename connect_handler>
auto client::async_connect(const io::address& address, BOOST_ASIO_MOVE_ARG(connect_handler) handler)
{
  return boost::asio::async_initiate<connect_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, const io::address& address) {
        this->async_connect_internal(address, std::forward<decltype(handler)>(handler));
      },
      handler, address);
}

template <typename connect_handler>
auto client::async_connect(BOOST_ASIO_MOVE_ARG(connect_handler) handler)
{
  return boost::asio::async_initiate<connect_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler) {
        this->async_connect_internal(std::forward<decltype(handler)>(handler));
      },
      handler);
}

template <typename create_tunnel_handler>
auto client::async_create_tunnel(const tunnel_properties& properties, BOOST_ASIO_MOVE_ARG(create_tunnel_handler) handler)
{
  return boost::asio::async_initiate<create_tunnel_handler, void(const boost::system::error_code&, tunnel)>(
      [this](auto&& handler, const tunnel_properties& properties) {
        this->async_create_tunnel_internal(properties, std::forward<decltype(handler)>(handler));
      },
      handler, properties);
}

}  // namespace io_rstrm
}  // namespace rstream
