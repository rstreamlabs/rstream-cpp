// See LICENSE file in the project root for license information.

#include "acceptor.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/socket_base.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>

#include "acceptor_ssl.hpp"
#include "error.hpp"
#include "factory.hpp"
#include "object_base.hpp"
#include "ssl.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

class RSTREAM_GNUC_INTERNAL acceptor::impl {
 public:
  impl(const executor_type& executor);

  impl(acceptor_ptr native_handle);

  virtual ~impl() = default;

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  void close(boost::system::error_code& error_code);

  void bind(const endpoint& endpoint, boost::system::error_code& error_code);

  void listen(int backlog, boost::system::error_code& error_code);

  endpoint local_endpoint(boost::system::error_code& error_code);

  void async_accept(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  acceptor_const_ptr native_handle() const;

  acceptor_ptr native_handle();

  void swap(acceptor_ptr native_handle);

 private:
  bool initialized() const;

  void init(const endpoint& endpoint, boost::system::error_code& error_code);

  executor_type m_executor;

  acceptor_ptr m_native_handle;
};

acceptor::acceptor(const executor_type& executor)
    : acceptor_base(executor),
      m_impl(std::make_shared<impl>(executor))
{
}

acceptor::acceptor(const executor_type& executor, const endpoint& endpoint)
    : acceptor(executor)
{
  open(endpoint);
  bind(endpoint);
  listen(boost::asio::socket_base::max_listen_connections);
}

acceptor::acceptor(acceptor_ptr native_handle)
    : acceptor_base(native_handle->get_executor()),
      m_impl(std::make_shared<impl>(native_handle))
{
}

void acceptor::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->open(endpoint, error_code);
}

void acceptor::close(boost::system::error_code& error_code)
{
  m_impl->close(error_code);
}

void acceptor::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->bind(endpoint, error_code);
}

void acceptor::listen(int backlog, boost::system::error_code& error_code)
{
  m_impl->listen(backlog, error_code);
}

endpoint acceptor::local_endpoint(boost::system::error_code& error_code)
{
  return m_impl->local_endpoint(error_code);
}

void acceptor::async_accept_internal(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  m_impl->async_accept(peer, endpoint, std::move(handler));
}

acceptor_const_ptr acceptor::native_handle() const
{
  return m_impl->native_handle();
}

acceptor_ptr acceptor::native_handle()
{
  return m_impl->native_handle();
}

void acceptor::swap(acceptor_ptr native_handle)
{
  m_impl->swap(native_handle);
}

acceptor::impl::impl(const executor_type& executor)
    : m_executor(executor)
{
}

acceptor::impl::impl(acceptor_ptr native_handle)
    : m_executor(native_handle->get_executor()),
      m_native_handle(native_handle)
{
}

void acceptor::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  if (!initialized()) {
    init(endpoint, error_code);
  }
  if (!error_code) {
    m_native_handle->open(endpoint, error_code);
  }
}

void acceptor::impl::close(boost::system::error_code& error_code)
{
  if (m_native_handle) {
    m_native_handle->close(error_code);
  }
  m_native_handle = nullptr;
}

void acceptor::impl::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  if (m_native_handle) {
    m_native_handle->bind(endpoint, error_code);
  }
  else {
    error_code = error::code::uninitialized_object;
  }
}

void acceptor::impl::listen(int backlog, boost::system::error_code& error_code)
{
  if (m_native_handle) {
    m_native_handle->listen(backlog, error_code);
  }
  else {
    error_code = error::code::uninitialized_object;
  }
}

endpoint acceptor::impl::local_endpoint(boost::system::error_code& error_code)
{
  endpoint result;
  if (m_native_handle) {
    result = m_native_handle->local_endpoint(error_code);
  }
  else {
    error_code = error::code::uninitialized_object;
  }
  return result;
}

void acceptor::impl::async_accept(stream_socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  if (m_native_handle) {
    m_native_handle->async_accept(peer, endpoint, std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::uninitialized_object);
  }
}

acceptor_const_ptr acceptor::impl::native_handle() const
{
  return m_native_handle;
}

acceptor_ptr acceptor::impl::native_handle()
{
  return m_native_handle;
}

void acceptor::impl::swap(acceptor_ptr native_handle)
{
  m_native_handle = native_handle;
}

bool acceptor::impl::initialized() const
{
  return m_native_handle != nullptr;
}

void acceptor::impl::init(const endpoint& endpoint, boost::system::error_code& error_code)
{
  acceptor_ptr native_handle = default_factory()->acceptor(m_executor, endpoint.protocol(), error_code);
  if (error_code) {
    return;
  }
  const auto ssl_config = parse_ssl_config(endpoint.get_url(), error_code);
  if (error_code) {
    return;
  }
  if (ssl_config) {
    try {
      native_handle = std::make_shared<acceptor_ssl>(native_handle, ssl_config.get());
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
