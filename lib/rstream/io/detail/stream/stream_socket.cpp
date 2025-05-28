// See LICENSE file in the project root for license information.

#include "stream_socket.hpp"

#include <boost/asio/dispatch.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>

#include "error.hpp"
#include "factory.hpp"
#include "object_base.hpp"
#include "ssl.hpp"
#include "stream_socket_ssl.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class RSTREAM_GNUC_INTERNAL stream_socket::impl {
 public:
  impl(const executor_type& executor);

  impl(stream_socket_ptr native_handle);

  virtual ~impl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  void close(boost::system::error_code& error_code);

  endpoint remote_endpoint(boost::system::error_code& error_code);

  bool is_secure();

  void async_connect(const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler);

  void async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler);

  void async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler);

  stream_socket_const_ptr native_handle() const;

  stream_socket_ptr native_handle();

  void swap(stream_socket_ptr native_handle);

 private:
  bool initialized() const;

  void init(const endpoint& endpoint, boost::system::error_code& error_code);

  executor_type m_executor;

  stream_socket_ptr m_native_handle;
};

stream_socket::stream_socket(const executor_type& executor)
    : stream_socket_base(executor),
      m_impl(std::make_shared<impl>(executor))
{
}

stream_socket::stream_socket(stream_socket_ptr native_handle)
    : stream_socket_base(native_handle->get_executor()),
      m_impl(std::make_shared<impl>(native_handle))
{
}

void stream_socket::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->open(endpoint, error_code);
}

void stream_socket::close(boost::system::error_code& error_code)
{
  m_impl->close(error_code);
}

endpoint stream_socket::remote_endpoint(boost::system::error_code& error_code)
{
  return m_impl->remote_endpoint(error_code);
}

bool stream_socket::is_secure() const
{
  return m_impl->is_secure();
}

stream_socket_const_ptr stream_socket::native_handle() const
{
  return m_impl->native_handle();
}

stream_socket_ptr stream_socket::native_handle()
{
  return m_impl->native_handle();
}

void stream_socket::swap(stream_socket_ptr native_handle)
{
  m_impl->swap(native_handle);
}

void stream_socket::async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  m_impl->async_connect(endpoint, std::move(handler));
}

void stream_socket::async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  m_impl->async_write_some(buffer, std::move(handler));
}

void stream_socket::async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  m_impl->async_write_some(buffer, std::move(handler));
}

void stream_socket::async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  m_impl->async_read_some(buffer, std::move(handler));
}

void stream_socket::async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  m_impl->async_read_some(buffer, std::move(handler));
}

stream_socket::impl::impl(const executor_type& executor)
    : m_executor(executor)
{
}

stream_socket::impl::impl(stream_socket_ptr native_handle)
    : m_executor(native_handle->get_executor()),
      m_native_handle(native_handle)
{
}

void stream_socket::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  if (!initialized()) {
    init(endpoint, error_code);
  }
  if (!error_code) {
    m_native_handle->open(endpoint, error_code);
  }
}

void stream_socket::impl::close(boost::system::error_code& error_code)
{
  if (m_native_handle) {
    m_native_handle->close(error_code);
  }
  m_native_handle = nullptr;
}

endpoint stream_socket::impl::remote_endpoint(boost::system::error_code& error_code)
{
  endpoint result;
  if (m_native_handle) {
    result = m_native_handle->remote_endpoint(error_code);
  }
  else {
    error_code = error::code::uninitialized_object;
  }
  return result;
}

bool stream_socket::impl::is_secure()
{
  if (!initialized()) {
    return false;
  }
  else {
    return m_native_handle->is_secure();
  }
}

void stream_socket::impl::async_connect(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    init(endpoint, error_code);
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code);
  }
  else {
    m_native_handle->async_connect(endpoint, std::move(handler));
  }
}

void stream_socket::impl::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    error_code = error::code::uninitialized_object;
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, 0);
  }
  else {
    m_native_handle->async_write_some(buffer, std::move(handler));
  }
}

void stream_socket::impl::async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    error_code = error::code::uninitialized_object;
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, 0);
  }
  else {
    m_native_handle->async_write_some(buffer, std::move(handler));
  }
}

void stream_socket::impl::async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    error_code = error::code::uninitialized_object;
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, 0);
  }
  else {
    m_native_handle->async_read_some(buffer, std::move(handler));
  }
}

void stream_socket::impl::async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  boost::system::error_code error_code;
  if (!initialized()) {
    error_code = error::code::uninitialized_object;
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, 0);
  }
  else {
    m_native_handle->async_read_some(buffer, std::move(handler));
  }
}

stream_socket_const_ptr stream_socket::impl::native_handle() const
{
  return m_native_handle;
}

stream_socket_ptr stream_socket::impl::native_handle()
{
  return m_native_handle;
}

void stream_socket::impl::swap(stream_socket_ptr native_handle)
{
  m_native_handle = native_handle;
}

bool stream_socket::impl::initialized() const
{
  return m_native_handle != nullptr;
}

void stream_socket::impl::init(const endpoint& endpoint, boost::system::error_code& error_code)
{
  stream_socket_ptr native_handle = default_factory()->socket(m_executor, endpoint.protocol(), error_code);
  if (error_code) {
    return;
  }
  const auto ssl_config = parse_ssl_config(endpoint.get_url(), error_code);
  if (error_code) {
    return;
  }
  if (ssl_config) {
    try {
      native_handle = std::make_shared<stream_socket_ssl>(native_handle, ssl_config.get(), stream_socket_ssl::type::client);
    }
    catch (boost::system::system_error& system_error) {
      error_code = system_error.code();
    }
    catch (...) {
      error_code = error::code::generic_error;
    }
  }
  if (!error_code) {
    m_native_handle = native_handle;
  }
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
