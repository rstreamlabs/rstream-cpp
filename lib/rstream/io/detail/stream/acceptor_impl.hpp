// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/plugin/common.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/acceptor_base.hpp>

#include "endpoint.hpp"
#include "endpoint_impl.hpp"
#include "error.hpp"
#include "object_base.hpp"
#include "stream.hpp"
#include "stream_socket.hpp"
#include "stream_socket_impl.hpp"

#define SINGLE_ARG(...) __VA_ARGS__

namespace rstream {
namespace io {
namespace detail {
namespace stream {

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type = typename native_acceptor_type::endpoint_type>
class acceptor_impl : public acceptor_base<endpoint, stream_socket>, public object_base, public std::enable_shared_from_this<acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>> {
 public:
  using ptr = std::shared_ptr<acceptor_impl>;

  acceptor_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  acceptor_impl(const executor_type& executor, native_acceptor_type&& acceptor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  virtual ~acceptor_impl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  void bind(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void listen(int backlog, boost::system::error_code& error_code) override;

  endpoint local_endpoint(boost::system::error_code& error_code) override;

  native_acceptor_type& get();

  const native_acceptor_type& get() const;

 protected:
  rstream::core::logger m_logger;

 private:
  class async_accept_operation;

  void open_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code);

  void configure_internal(const native_endpoint_type& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);

  void bind_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code);

  native_endpoint_type local_endpoint_internal(boost::system::error_code& error_code);

  void async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler) override;

  void async_accept_internal(native_socket_type& peer, native_endpoint_type& endpoint, async_accept_completion_handler&& handler);

  native_acceptor_type m_acceptor;

  const endpoint_base::protocol_type::value_type m_protocol;

  const element_const_ptr m_parent_ptr;

  boost::urls::url m_url;
};

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
class acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_operation : public std::enable_shared_from_this<async_accept_operation> {
 public:
  async_accept_operation(ptr acceptor_ptr, stream_socket& peer, endpoint& endpoint, const boost::urls::url& url, async_accept_completion_handler&& handler);

  void run();

  boost::system::error_code complete(const boost::system::error_code& error_code);

 private:
  ptr m_acceptor_ptr;

  stream_socket& m_peer;

  endpoint& m_endpoint;

  const boost::urls::url m_url;

  native_socket_type m_socket;

  native_endpoint_type m_native_endpoint;

  async_accept_completion_handler m_handler;
};

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::acceptor_impl(const executor_type& executor, native_acceptor_type&& acceptor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : acceptor_base(executor),
      m_logger({"rstream", "io", "acceptor", fmt::format("#{}", fmt::ptr(this))}),
      m_acceptor(std::move(acceptor)),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::acceptor_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : acceptor_base(executor),
      m_logger({"rstream", "io", "acceptor", fmt::format("#{}", fmt::ptr(this))}),
      m_acceptor(executor),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  const auto native_endpoint = std::dynamic_pointer_cast<const endpoint_impl<native_endpoint_type>>(endpoint.native_handle());
  if (!native_endpoint) {
    error_code = error::code::invalid_argument;
    return;
  }
  open_internal(native_endpoint->get(), error_code);
  if (!error_code) {
    m_url = endpoint.get_url();
    configure_internal(native_endpoint->get(), m_url, error_code);
  }
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::close(boost::system::error_code& error_code)
{
  m_acceptor.close(error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  const auto native_endpoint = std::dynamic_pointer_cast<const endpoint_impl<native_endpoint_type>>(endpoint.native_handle());
  if (!native_endpoint) {
    error_code = error::code::invalid_argument;
    return;
  }
  bind_internal(native_endpoint->get(), error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::listen(int backlog, boost::system::error_code& error_code)
{
  m_acceptor.listen(backlog, error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
endpoint acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::local_endpoint(boost::system::error_code& error_code)
{
  auto local_endpoint = local_endpoint_internal(error_code);
  auto ptr            = std::make_shared<endpoint_impl<native_endpoint_type>>(local_endpoint, m_protocol, m_parent_ptr);
  return error_code ? endpoint() : endpoint(endpoint_ptr(ptr.get(), core::detail::plugin::object_deleter(ptr, m_parent_ptr)));
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
native_acceptor_type& acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::get()
{
  return m_acceptor;
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
const native_acceptor_type& acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::get() const
{
  return m_acceptor;
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::open_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code)
{
  m_acceptor.open(endpoint.protocol(), error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::configure_internal(const native_endpoint_type& endpoint, const boost::urls::url& url, boost::system::error_code& error_code)
{
  (void)endpoint;
  (void)url;
  (void)error_code;
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::bind_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code)
{
  m_acceptor.bind(endpoint, error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
native_endpoint_type acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::local_endpoint_internal(boost::system::error_code& error_code)
{
  return m_acceptor.local_endpoint(error_code);
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  std::make_shared<async_accept_operation>(
      std::enable_shared_from_this<acceptor_impl>::shared_from_this(),
      peer, endpoint, m_url, std::move(handler))
      ->run();
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_internal(native_socket_type& peer, native_endpoint_type& endpoint, async_accept_completion_handler&& handler)
{
  m_acceptor.async_accept(peer, endpoint, std::move(handler));
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_operation::async_accept_operation(ptr acceptor_ptr, stream_socket& peer, endpoint& endpoint, const boost::urls::url& url, async_accept_completion_handler&& handler)
    : m_acceptor_ptr(acceptor_ptr),
      m_peer(peer),
      m_endpoint(endpoint),
      m_url(url),
      m_socket(m_acceptor_ptr->get_executor()),
      m_handler(std::move(handler))
{
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
void acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_operation::run()
{
  auto ptr                = async_accept_operation::shared_from_this();
  auto completion_handler = rstream::core::bind_associated_handler(
      std::move(m_handler),
      [ptr](auto& handler, const boost::system::error_code& error_code) mutable {
        const auto executor = ptr->m_acceptor_ptr->get_executor();
        const auto cause    = ptr->complete(error_code);
        ptr.reset();
        rstream::core::invoke_completion_handler(executor, std::move(handler), cause);
      });
  m_acceptor_ptr->async_accept_internal(m_socket, m_native_endpoint, std::move(completion_handler));
}

template <typename native_acceptor_type, typename native_socket_type, typename native_endpoint_type>
boost::system::error_code acceptor_impl<native_acceptor_type, native_socket_type, native_endpoint_type>::async_accept_operation::complete(const boost::system::error_code& error_code)
{
  auto cause         = error_code;
  auto peer_ptr      = std::make_shared<stream_socket_impl<native_socket_type, native_endpoint_type>>(m_acceptor_ptr->get_executor(), std::move(m_socket), m_acceptor_ptr->m_protocol, m_acceptor_ptr->m_parent_ptr);
  auto endpoint_ptr_ = std::make_shared<endpoint_impl<native_endpoint_type>>(std::move(m_native_endpoint), m_acceptor_ptr->m_protocol, m_acceptor_ptr->m_parent_ptr);
  if (!cause) {
    peer_ptr->configure_internal(socket_mode::server, true, endpoint_ptr_->get(), m_url, cause);
  }
  if (!cause) {
    m_peer.swap(stream_socket_ptr(peer_ptr.get(), core::detail::plugin::object_deleter(peer_ptr, m_acceptor_ptr->m_parent_ptr)));
    m_endpoint = endpoint(endpoint_ptr(endpoint_ptr_.get(), core::detail::plugin::object_deleter(endpoint_ptr_, m_acceptor_ptr->m_parent_ptr)));
  }
  else if (!error_code) {
    boost::system::error_code tmp;
    peer_ptr->close(tmp);
  }
  return cause;
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
