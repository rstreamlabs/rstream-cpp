// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/bind_executor.hpp>
#include <boost/system/error_code.hpp>
#include <boost/url.hpp>

#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/plugin/common.hpp>
#include <rstream/core/log.hpp>
#include <rstream/io/stream_socket_base.hpp>

#include "endpoint.hpp"
#include "endpoint_impl.hpp"
#include "object_base.hpp"
#include "stream.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

template <typename native_socket_type, typename native_endpoint_type = typename native_socket_type::endpoint_type>
class stream_socket_impl : public stream_socket_base<endpoint>, public object_base, public std::enable_shared_from_this<stream_socket_impl<native_socket_type, native_endpoint_type>> {
 public:
  template <typename X, typename Y, typename Z>
  friend class acceptor_impl;

  using ptr = std::shared_ptr<stream_socket_impl>;

  stream_socket_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  stream_socket_impl(const executor_type& executor, native_socket_type&& socket, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr);

  virtual ~stream_socket_impl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code) override;

  void close(boost::system::error_code& error_code) override;

  endpoint remote_endpoint(boost::system::error_code& error_code) override;

  native_socket_type& get();

  const native_socket_type& get() const;

 protected:
  rstream::core::logger m_logger;

 private:
  class async_connect_operation;

  void open_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code);

  void configure_internal(socket_mode mode, bool connected, const native_endpoint_type& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);

  native_endpoint_type remote_endpoint_internal(boost::system::error_code& error_code);

  void async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler) override;

  void async_connect_internal(const native_endpoint_type& endpoint, async_connect_completion_handler&& handler);

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) override;

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) override;

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) override;

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) override;

  native_socket_type m_socket;

  const endpoint_base::protocol_type::value_type m_protocol;

  const element_const_ptr m_parent_ptr;
};

template <typename native_socket_type, typename native_endpoint_type>
class stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_operation : public std::enable_shared_from_this<async_connect_operation> {
 public:
  async_connect_operation(ptr stream_socket_ptr, const native_endpoint_type& endpoint, const boost::urls::url& url, async_connect_completion_handler&& handler);

  void run();

  void on_complete(const boost::system::error_code& error_code);

 private:
  ptr m_stream_socket_ptr;

  const native_endpoint_type m_endpoint;

  const boost::urls::url m_url;

  async_connect_completion_handler m_handler;
};

template <typename native_socket_type, typename native_endpoint_type>
stream_socket_impl<native_socket_type, native_endpoint_type>::stream_socket_impl(const executor_type& executor, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : stream_socket_base(executor),
      m_logger({"rstream", "io", "stream_socket", fmt::format("#{}", fmt::ptr(this))}),
      m_socket(executor),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_socket_type, typename native_endpoint_type>
stream_socket_impl<native_socket_type, native_endpoint_type>::stream_socket_impl(const executor_type& executor, native_socket_type&& socket, const endpoint_base::protocol_type::value_type& protocol, element_const_ptr parent_ptr)
    : stream_socket_base(executor),
      m_logger({"rstream", "io", "stream_socket", fmt::format("#{}", fmt::ptr(this))}),
      m_socket(std::move(socket)),
      m_protocol(protocol),
      m_parent_ptr(parent_ptr)
{
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  const auto& native_endpoint = std::dynamic_pointer_cast<const endpoint_impl<native_endpoint_type>>(endpoint.native_handle())->get();
  open_internal(native_endpoint, error_code);
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::close(boost::system::error_code& error_code)
{
  m_socket.close(error_code);
}

template <typename native_socket_type, typename native_endpoint_type>
endpoint stream_socket_impl<native_socket_type, native_endpoint_type>::remote_endpoint(boost::system::error_code& error_code)
{
  auto remote_endpoint = remote_endpoint_internal(error_code);
  if (error_code) {
    return endpoint();
  }
  auto ptr = std::make_shared<endpoint_impl<native_endpoint_type>>(remote_endpoint, m_protocol, m_parent_ptr);
  return endpoint(endpoint_ptr(ptr.get(), core::detail::plugin::object_deleter(ptr, m_parent_ptr)));
}

template <typename native_socket_type, typename native_endpoint_type>
native_socket_type& stream_socket_impl<native_socket_type, native_endpoint_type>::get()
{
  return m_socket;
}

template <typename native_socket_type, typename native_endpoint_type>
const native_socket_type& stream_socket_impl<native_socket_type, native_endpoint_type>::get() const
{
  return m_socket;
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::open_internal(const native_endpoint_type& endpoint, boost::system::error_code& error_code)
{
  m_socket.open(endpoint.protocol(), error_code);
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::configure_internal(socket_mode mode, bool connected, const native_endpoint_type& endpoint, const boost::urls::url& url, boost::system::error_code& error_code)
{
  (void)mode;
  (void)connected;
  (void)endpoint;
  (void)url;
  (void)error_code;
}

template <typename native_socket_type, typename native_endpoint_type>
native_endpoint_type stream_socket_impl<native_socket_type, native_endpoint_type>::remote_endpoint_internal(boost::system::error_code& error_code)
{
  return m_socket.remote_endpoint(error_code);
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  boost::system::error_code error_code;
  const native_endpoint_type& native_endpoint = std::dynamic_pointer_cast<const endpoint_impl<native_endpoint_type>>(endpoint.native_handle())->get();
  configure_internal(socket_mode::client, false, native_endpoint, endpoint.get_url(), error_code);
  if (error_code) {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), error_code);
  }
  else {
    std::make_shared<async_connect_operation>(std::enable_shared_from_this<stream_socket_impl>::shared_from_this(), std::dynamic_pointer_cast<const endpoint_impl<native_endpoint_type>>(endpoint.native_handle())->get(), endpoint.get_url(), std::move(handler))->run();
  }
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_internal(const native_endpoint_type& endpoint, async_connect_completion_handler&& handler)
{
  m_socket.async_connect(endpoint, std::move(handler));
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  m_socket.async_write_some(buffer, std::move(handler));
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  m_socket.async_write_some(buffer, std::move(handler));
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  m_socket.async_read_some(buffer, std::move(handler));
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  m_socket.async_read_some(buffer, std::move(handler));
}

template <typename native_socket_type, typename native_endpoint_type>
stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_operation::async_connect_operation(ptr stream_socket_ptr, const native_endpoint_type& endpoint, const boost::urls::url& url, async_connect_completion_handler&& handler)
    : m_stream_socket_ptr(stream_socket_ptr),
      m_endpoint(endpoint),
      m_url(url),
      m_handler(std::move(handler))
{
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_operation::run()
{
  auto executor           = boost::asio::get_associated_executor(m_handler, m_stream_socket_ptr->get_executor());
  auto completion_handler = std::bind(&async_connect_operation::on_complete, async_connect_operation::shared_from_this(), std::placeholders::_1);
  m_stream_socket_ptr->async_connect_internal(m_endpoint, boost::asio::bind_executor(executor, completion_handler));
}

template <typename native_socket_type, typename native_endpoint_type>
void stream_socket_impl<native_socket_type, native_endpoint_type>::async_connect_operation::on_complete(const boost::system::error_code& error_code)
{
  auto cause = error_code;
  if (!cause) {
    m_stream_socket_ptr->configure_internal(socket_mode::client, true, m_endpoint, m_url, cause);
  }
  if (cause && !error_code) {
    boost::system::error_code tmp;
    m_stream_socket_ptr->close(tmp);
  }
  rstream::core::invoke_completion_handler(m_stream_socket_ptr->get_executor(), std::move(m_handler), cause);
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
