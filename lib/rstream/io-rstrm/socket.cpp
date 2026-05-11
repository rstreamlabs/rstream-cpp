// See LICENSE file in the project root for license information.

#include "socket.hpp"

#include <memory>
#include <mutex>

#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/optional.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>
#endif

#include "detail/handshake.hpp"

namespace rstream {
namespace io_rstrm {

class RSTREAM_GNUC_INTERNAL socket::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, core::allocator::ptr allocator);

  virtual ~impl() = default;

  settings_socket settings(boost::system::error_code& error_code);

  void settings(const settings_socket& settings, boost::system::error_code& error_code);

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  endpoint remote_endpoint(boost::system::error_code& error_code);

  void async_connect(type type, const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler);

  void async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler);

  void async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler);

  void close(boost::system::error_code& error_code);

 private:
  enum class state {
    null       = 0,
    connecting = 1,
    connected  = 2,
  };

#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = io::stream;
#else
  using protocol_type = boost::asio::ip::tcp;
#endif

  using resolver_type = protocol_type::resolver;

  using socket_type = protocol_type::socket;

  using handshake_type = detail::handshake<protocol_type::socket&>;

  void async_connect_internal(type type, const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler);

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler);

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_connect(const resolver_type::results_type& results);

  void on_connect(const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint);

  void do_handshake();

  void on_handshake(const boost::system::error_code& error_code);

  void on_started();

  void close_internal();

  void on_error(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  settings_socket m_settings;

  core::logger m_logger;

  state m_state;

  core::allocator::ptr m_allocator;

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  resolver_type m_resolver;

  socket_type m_socket;

  type m_type;

  endpoint m_endpoint;

  async_connect_completion_handler m_handler;

  std::mutex m_mutex;

  bool m_is_state_non_null;

  boost::optional<endpoint> m_remote_endpoint;
};

socket::socket(const executor_type& executor, core::allocator::ptr allocator)
    : io::stream_socket_base<endpoint>(executor),
      m_allocator(allocator)
{
}

socket::socket(const executor_type& executor, const settings_socket& settings, core::allocator::ptr allocator)
    : socket(executor, allocator)
{
  boost::system::error_code error_code;
  this->settings(settings, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

socket::socket(socket&& other) noexcept
    : io::stream_socket_base<endpoint>(other.get_executor())
{
  m_allocator  = other.m_allocator;
  m_impl       = other.m_impl;
  other.m_impl = nullptr;
}

socket& socket::operator=(socket&& other) noexcept
{
  if (this != &other) {
    io::stream_socket_base<endpoint>::operator=(std::move(other));
    m_allocator  = other.m_allocator;
    m_impl       = other.m_impl;
    other.m_impl = nullptr;
  }
  return *this;
}

socket::socket(const socket& other) noexcept
    : io::stream_socket_base<endpoint>(other.get_executor())
{
  m_allocator = other.m_allocator;
  m_impl      = other.m_impl;
}

socket& socket::operator=(const socket& other) noexcept
{
  if (this != &other) {
    io::stream_socket_base<endpoint>::operator=(other);
    m_allocator = other.m_allocator;
    m_impl      = other.m_impl;
  }
  return *this;
}

settings_socket socket::settings(boost::system::error_code& error_code)
{
  return ptr()->settings(error_code);
}

settings_socket socket::settings()
{
  boost::system::error_code error_code;
  auto settings = ptr()->settings(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return settings;
}

void socket::settings(const settings_socket& settings, boost::system::error_code& error_code)
{
  return ptr()->settings(settings, error_code);
}

void socket::settings(const settings_socket& settings)
{
  boost::system::error_code error_code;
  ptr()->settings(settings, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

void socket::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  return ptr()->open(endpoint, error_code);
}

void socket::open(const endpoint& endpoint)
{
  boost::system::error_code error_code;
  open(endpoint, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

void socket::close(boost::system::error_code& error_code)
{
  return ptr()->close(error_code);
}

void socket::close()
{
  boost::system::error_code error_code;
  close(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

endpoint socket::remote_endpoint(boost::system::error_code& error_code)
{
  return ptr()->remote_endpoint(error_code);
}

endpoint socket::remote_endpoint()
{
  boost::system::error_code error_code;
  auto endpoint = ptr()->remote_endpoint(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return endpoint;
}

void socket::async_connect_internal(type type, const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  return ptr()->async_connect(type, endpoint, std::move(handler));
}

void socket::async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  return async_connect_internal(type::client, endpoint, std::move(handler));
}

void socket::async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  return ptr()->async_write_some(buffer, std::move(handler));
}

void socket::async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  return ptr()->async_write_some(buffer, std::move(handler));
}

void socket::async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  return ptr()->async_read_some(buffer, std::move(handler));
}

void socket::async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  return ptr()->async_read_some(buffer, std::move(handler));
}

std::shared_ptr<socket::impl> socket::ptr()
{
  if (!m_impl) {
    m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(m_allocator), get_executor(), m_allocator);
  }
  return m_impl;
}

socket::impl::impl(const executor_type& executor, core::allocator::ptr allocator)
    : m_logger({"rstream", "io-rstrm", "socket", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null),
      m_allocator(allocator),
      m_executor(executor),
      m_strand(executor),
      m_resolver(executor),
      m_socket(executor),
      m_is_state_non_null(false)
{
}

settings_socket socket::impl::settings(boost::system::error_code& error_code)
{
  (void)error_code;
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_settings;
}

void socket::impl::settings(const settings_socket& settings, boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  else {
    m_settings = settings;
  }
}

void socket::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  (void)endpoint;
  (void)error_code;
}

void socket::impl::close(boost::system::error_code& error_code)
{
  (void)error_code;
  boost::asio::dispatch(m_strand, std::bind_front(&impl::close_internal, shared_from_this()));
}

endpoint socket::impl::remote_endpoint(boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_remote_endpoint) {
    return m_remote_endpoint.get();
  }
  error_code = error::code::invalid_state;
  return endpoint();
}

void socket::impl::async_connect(type type, const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_connect_internal, shared_from_this(), type, endpoint, std::move(handler)));
}

void socket::impl::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const boost::asio::const_buffer&, async_write_some_completion_handler&&)) & impl::async_write_some_internal, shared_from_this(), buffer, std::move(handler)));
}

void socket::impl::async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const const_buffer_sequence_type&, async_write_some_completion_handler&&)) & impl::async_write_some_internal, shared_from_this(), buffer, std::move(handler)));
}

void socket::impl::async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const boost::asio::mutable_buffer&, async_read_some_completion_handler&&)) & impl::async_read_some_internal, shared_from_this(), buffer, std::move(handler)));
}

void socket::impl::async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const mutable_buffer_sequence_type&, async_read_some_completion_handler&&)) & impl::async_read_some_internal, shared_from_this(), buffer, std::move(handler)));
}

void socket::impl::async_connect_internal(type type, const endpoint& endpoint, async_connect_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state != state::null) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state);
  }
  else if (endpoint.m_server_address_from_uri_param && !m_settings.m_config.m_no_token && !m_settings.m_config.m_token_from_uri_param) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_configuration);
  }
  else if (!endpoint.m_id_name) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_endpoint);
  }
  else {
    m_type     = type;
    m_endpoint = endpoint;
    m_handler.swap(handler);
    m_state = state::connecting;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_is_state_non_null = true;
    }
    do_resolve_host();
  }
}

void socket::impl::async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state == state::connected) {
    m_socket.async_write_some(buffer, std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
  }
}

void socket::impl::async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state == state::connected) {
    m_socket.async_write_some(buffer, std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
  }
}

void socket::impl::async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state == state::connected) {
    m_socket.async_read_some(buffer, std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
  }
}

void socket::impl::async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state == state::connected) {
    m_socket.async_read_some(buffer, std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, 0);
  }
}

void socket::impl::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_endpoint.m_server_address.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_endpoint.m_server_address.host(), m_endpoint.m_server_address.port(), boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void socket::impl::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  auto cause = error_code;
  if (!cause && results.empty()) {
    cause = error::code::no_valid_endpoint;
  }
  if (cause) {
    on_error(cause);
  }
  else {
    do_connect(results);
  }
}

void socket::impl::do_connect(const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_connect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::asio::async_connect(m_socket, results, boost::asio::bind_executor(m_strand, completion_handler));
}

void socket::impl::on_connect(const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_handshake();
  }
}

void socket::impl::do_handshake()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_handshake, shared_from_this(), std::placeholders::_1);
  const auto type         = m_type == type::client ? handshake_type::type::stream_req : handshake_type::type::proxy_req;
  auto handshake          = std::allocate_shared<handshake_type>(core::allocator::wrapper<impl>(m_allocator), m_socket, m_endpoint.m_server_address, m_settings.m_config, m_allocator);
  handshake->async_run(type, m_endpoint.m_id_name.get(), m_endpoint.m_secret, boost::asio::bind_executor(m_strand, completion_handler));
}

void socket::impl::on_handshake(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    on_started();
  }
}

void socket::impl::on_started()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state             = state::connected;
  m_endpoint.m_secret = boost::none;  // clear secret
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_remote_endpoint = m_endpoint;
  }
  rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), boost::system::error_code());
  m_handler = nullptr;
}

void socket::impl::close_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::null) {
    on_close(boost::system::error_code());
  }
}

void socket::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void socket::impl::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state::null;
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), error_code);
  }
  m_handler  = nullptr;
  m_endpoint = endpoint();
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_is_state_non_null = false;
    m_remote_endpoint.reset();
  }
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
    m_resolver.cancel();
  }
}

}  // namespace io_rstrm
}  // namespace rstream
