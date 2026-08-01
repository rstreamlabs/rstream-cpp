// See LICENSE file in the project root for license information.

#pragma once

#include <memory>
#include <string>

#include <boost/asio/async_result.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/io_object.hpp>

#include "endpoint.hpp"
#include "socket.hpp"

namespace rstream {
namespace io_rstrm {

class client;

class tunnel {
  friend class client;

 public:
  tunnel(std::nullptr_t);

  tunnel();

  tunnel(const tunnel&) = default;

  tunnel& operator=(const tunnel&) = default;

  tunnel(tunnel&&) noexcept = default;

  tunnel& operator=(tunnel&&) noexcept = default;

  virtual ~tunnel() = default;

  operator bool() const noexcept;

  // get the tunnel ID
  endpoint local_endpoint(boost::system::error_code& error_code);
  endpoint local_endpoint();

  // get tunnel properties
  tunnel_properties properties(boost::system::error_code& error_code);
  tunnel_properties properties();

  // accept incoming connections
  template <typename accept_handler>
  auto async_accept(socket& peer, endpoint& endpoint, BOOST_ASIO_MOVE_ARG(accept_handler) handler);

  // close the tunnel
  void close();

 private:
  class impl;

  tunnel(std::shared_ptr<impl> impl);

  using async_accept_completion_handler = core::completion_handler<void(const boost::system::error_code&)>;
  void async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  // implementation details
  std::shared_ptr<impl> m_impl;
};

template <typename accept_handler>
auto tunnel::async_accept(socket& peer, endpoint& endpoint, BOOST_ASIO_MOVE_ARG(accept_handler) handler)
{
  return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&)>(
      [this](auto&& handler, socket* peer, struct endpoint* peer_endpoint) {
        async_accept_internal(*peer, *peer_endpoint, std::forward<decltype(handler)>(handler));
      },
      handler, &peer, &endpoint);
}

}  // namespace io_rstrm
}  // namespace rstream
