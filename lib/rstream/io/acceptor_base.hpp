// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/async_result.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>

#include "socket_base.hpp"

#define SINGLE_ARG(...) __VA_ARGS__

namespace rstream {
namespace io {

template <class T, class S>
class acceptor_base : public socket_base<T> {
 public:
  using endpoint_type = T;

  using socket_type = S;

  acceptor_base(const io_object::executor_type& executor);

  virtual ~acceptor_base() = default;

  virtual void bind(const endpoint_type& endpoint, boost::system::error_code& error_code) = 0;

  void bind(const endpoint_type& endpoint);

  virtual void listen(int backlog, boost::system::error_code& error_code) = 0;  // TODO : Remove this method in future release

  void listen(int backlog);  // TODO : Remove this method in future release

  virtual endpoint_type local_endpoint(boost::system::error_code& error_code) = 0;

  endpoint_type local_endpoint();

  using async_accept_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  template <typename accept_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(accept_handler), void(const boost::system::error_code&))
  async_accept(socket_type& peer, endpoint_type& endpoint, BOOST_ASIO_MOVE_ARG(accept_handler) handler)
  {
    return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&)>([this, &peer, &endpoint](auto&& handler) { async_accept_internal(peer, endpoint, std::forward<decltype(handler)>(handler)); }, handler);
  }

  template <typename accept_handler>
  BOOST_ASIO_INITFN_RESULT_TYPE(BOOST_ASIO_MOVE_ARG(accept_handler), void(const boost::system::error_code&, socket_type))
  async_accept(BOOST_ASIO_MOVE_ARG(accept_handler) handler)
  {
    return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&, socket_type)>(
        [this](auto&& handler) {
          async_accept_internal(
              m_peer, m_endpoint,
              [this, handler = std::move(handler)](const boost::system::error_code& error_code) mutable {
                rstream::core::invoke_completion_handler(io_object::get_executor(), std::move(handler), error_code, std::move(m_peer));
              });
        },
        handler);
  }

 private:
  virtual void async_accept_internal(socket_type& peer, endpoint_type& endpoint, async_accept_completion_handler&& handler) = 0;

  socket_type m_peer;

  endpoint_type m_endpoint;
};

template <class T, class S>
acceptor_base<T, S>::acceptor_base(const io_object::executor_type& executor)
    : socket_base<T>(executor),
      m_peer(executor)
{
}

template <class T, class S>
void acceptor_base<T, S>::bind(const endpoint_type& endpoint)
{
  boost::system::error_code error_code;
  bind(endpoint, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

template <class T, class S>
void acceptor_base<T, S>::listen(int backlog)
{
  boost::system::error_code error_code;
  listen(backlog, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

template <class T, class S>
typename acceptor_base<T, S>::endpoint_type acceptor_base<T, S>::local_endpoint()
{
  endpoint_type endpoint;
  boost::system::error_code error_code;
  endpoint = local_endpoint(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return endpoint;
}

}  // namespace io
}  // namespace rstream
