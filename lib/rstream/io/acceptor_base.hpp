// See LICENSE file in the project root for license information.

#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/system/error_code.hpp>

#include <rstream/core/completion_handler.hpp>

#include "socket_base.hpp"

#define SINGLE_ARG(...) __VA_ARGS__

namespace rstream {
namespace io {

template <class T, class S>
class acceptor_base : public socket_base<T> {
  template <typename Handler>
  struct owning_accept_operation {
    owning_accept_operation(const io_object::executor_type& executor, Handler&& handler)
        : m_peer(executor),
          m_handler(std::move(handler))
    {
    }

    S m_peer;
    T m_endpoint;
    Handler m_handler;
    boost::asio::cancellation_signal m_cancellation;
  };

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
  auto async_accept(socket_type& peer, endpoint_type& endpoint, BOOST_ASIO_MOVE_ARG(accept_handler) handler)
  {
    return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&)>(
        [this](auto&& handler, socket_type* peer, endpoint_type* peer_endpoint) {
          async_accept_internal(*peer, *peer_endpoint, std::forward<decltype(handler)>(handler));
        },
        handler, &peer, &endpoint);
  }

  template <typename accept_handler>
  auto async_accept(BOOST_ASIO_MOVE_ARG(accept_handler) handler)
  {
    return boost::asio::async_initiate<accept_handler, void(const boost::system::error_code&, socket_type)>(
        [this](auto&& handler) {
          using operation_type   = owning_accept_operation<std::decay_t<decltype(handler)>>;
          auto executor          = io_object::get_executor();
          auto allocator         = boost::asio::get_associated_allocator(handler);
          auto operation         = std::allocate_shared<operation_type>(allocator, executor, std::forward<decltype(handler)>(handler));
          auto cancellation_slot = boost::asio::get_associated_cancellation_slot(operation->m_handler);
          if (cancellation_slot.is_connected()) {
            const std::weak_ptr<operation_type> weak_operation = operation;
            cancellation_slot.assign([weak_operation](boost::asio::cancellation_type cancellation_type) {
              const auto operation = weak_operation.lock();
              if (operation && cancellation_type != boost::asio::cancellation_type::none) {
                operation->m_cancellation.emit(cancellation_type);
              }
            });
          }
          auto completion_handler = boost::asio::bind_cancellation_slot(
              operation->m_cancellation.slot(),
              [executor, operation](const boost::system::error_code& error_code) mutable {
                auto handler = std::move(operation->m_handler);
                auto peer    = std::move(operation->m_peer);
                operation.reset();
                rstream::core::invoke_completion_handler(executor, std::move(handler), error_code, std::move(peer));
              });
          async_accept_internal(operation->m_peer, operation->m_endpoint, std::move(completion_handler));
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
