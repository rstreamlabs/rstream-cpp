// See LICENSE file in the project root for license information.

#include "socket.hpp"

#include <atomic>
#include <memory>
#include <mutex>

#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/cancellation_signal.hpp>
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

  struct connect_op {
    using ptr = std::shared_ptr<connect_op>;
    explicit connect_op(async_connect_completion_handler&& handler);
    std::atomic_bool m_cancelled;
    bool m_completed;
    bool m_started;
    boost::system::error_code m_terminal_error;
    async_connect_completion_handler m_handler;
  };

  struct transfer_op {
    using ptr = std::shared_ptr<transfer_op>;
    explicit transfer_op(async_write_some_completion_handler&& handler);
    std::atomic_bool m_cancelled;
    bool m_completed;
    bool m_started;
    async_write_some_completion_handler m_handler;
    boost::asio::cancellation_signal m_cancellation;
  };

  void async_connect_internal(type type, const endpoint& endpoint, const connect_op::ptr& op);

  void async_write_some_internal(const boost::asio::const_buffer& buffer, const transfer_op::ptr& op);

  void async_write_some_internal(const const_buffer_sequence_type& buffer, const transfer_op::ptr& op);

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, const transfer_op::ptr& op);

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, const transfer_op::ptr& op);

  void install_transfer_cancellation(const transfer_op::ptr& op);

  void cancel_connect(const connect_op::ptr& op);

  void cancel_transfer(const transfer_op::ptr& op);

  void complete_connect(const connect_op::ptr& op, const boost::system::error_code& error_code);

  void complete_transfer(const transfer_op::ptr& op, const boost::system::error_code& error_code, std::size_t transferred);

  void on_transfer(const transfer_op::ptr& op, const boost::system::error_code& error_code, std::size_t transferred);

  void do_resolve_host();

  void on_do_resolve_host(const connect_op::ptr& op, const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_connect(const resolver_type::results_type& results);

  void on_connect(const connect_op::ptr& op, const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint);

  void do_handshake();

  void on_handshake(const connect_op::ptr& op, const boost::system::error_code& error_code);

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

  connect_op::ptr m_connect_op;

  std::mutex m_mutex;

  bool m_is_state_non_null;

  boost::optional<endpoint> m_remote_endpoint;
};

socket::impl::connect_op::connect_op(async_connect_completion_handler&& handler)
    : m_cancelled(false),
      m_completed(false),
      m_started(false),
      m_handler(std::move(handler))
{
}

socket::impl::transfer_op::transfer_op(async_write_some_completion_handler&& handler)
    : m_cancelled(false),
      m_completed(false),
      m_started(false),
      m_handler(std::move(handler))
{
}

socket::socket(const executor_type& executor, core::allocator::ptr allocator)
    : io::stream_socket_base<endpoint>(executor),
      m_allocator(allocator),
      m_impl(std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), executor, allocator))
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
    : io::stream_socket_base<endpoint>(other.get_executor()),
      m_allocator(std::move(other.m_allocator)),
      m_impl(std::move(other.m_impl))
{
}

socket& socket::operator=(socket&& other) noexcept
{
  if (this != &other) {
    auto allocator = std::move(other.m_allocator);
    auto impl      = std::move(other.m_impl);
    io::stream_socket_base<endpoint>::operator=(other);
    m_allocator = std::move(allocator);
    m_impl      = std::move(impl);
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

void socket::adopt_impl(socket&& other) noexcept
{
  if (this != &other) {
    m_impl       = std::move(other.m_impl);
    other.m_impl = nullptr;
  }
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
  if (!handler) {
    return;
  }
  auto allocator         = boost::asio::get_associated_allocator(handler);
  const auto op          = std::allocate_shared<connect_op>(allocator, std::move(handler));
  auto cancellation_slot = boost::asio::get_associated_cancellation_slot(op->m_handler);
  if (cancellation_slot.is_connected()) {
    const std::weak_ptr<impl> weak_self     = shared_from_this();
    const std::weak_ptr<connect_op> weak_op = op;
    cancellation_slot.assign([weak_self, weak_op](boost::asio::cancellation_type cancellation_type) {
      if (cancellation_type == boost::asio::cancellation_type::none) {
        return;
      }
      const auto op = weak_op.lock();
      if (!op) {
        return;
      }
      op->m_cancelled.store(true, std::memory_order_release);
      const auto self = weak_self.lock();
      if (self) {
        boost::asio::dispatch(self->m_strand, [self, op] { self->cancel_connect(op); });
      }
    });
  }
  boost::asio::dispatch(
      m_strand,
      core::bind_handler_allocator(
          allocator,
          [self = shared_from_this(), type, endpoint, op] { self->async_connect_internal(type, endpoint, op); }));
}

void socket::impl::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  if (!handler) {
    return;
  }
  auto allocator = boost::asio::get_associated_allocator(handler);
  const auto op  = std::allocate_shared<transfer_op>(allocator, std::move(handler));
  install_transfer_cancellation(op);
  boost::asio::dispatch(
      m_strand,
      core::bind_handler_allocator(
          allocator,
          [self = shared_from_this(), buffer, op] { self->async_write_some_internal(buffer, op); }));
}

void socket::impl::async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  if (!handler) {
    return;
  }
  auto allocator = boost::asio::get_associated_allocator(handler);
  const auto op  = std::allocate_shared<transfer_op>(allocator, std::move(handler));
  install_transfer_cancellation(op);
  boost::asio::dispatch(
      m_strand,
      core::bind_handler_allocator(
          allocator,
          [self = shared_from_this(), buffer, op] { self->async_write_some_internal(buffer, op); }));
}

void socket::impl::async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  if (!handler) {
    return;
  }
  auto allocator = boost::asio::get_associated_allocator(handler);
  const auto op  = std::allocate_shared<transfer_op>(allocator, std::move(handler));
  install_transfer_cancellation(op);
  boost::asio::dispatch(
      m_strand,
      core::bind_handler_allocator(
          allocator,
          [self = shared_from_this(), buffer, op] { self->async_read_some_internal(buffer, op); }));
}

void socket::impl::async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  if (!handler) {
    return;
  }
  auto allocator = boost::asio::get_associated_allocator(handler);
  const auto op  = std::allocate_shared<transfer_op>(allocator, std::move(handler));
  install_transfer_cancellation(op);
  boost::asio::dispatch(
      m_strand,
      core::bind_handler_allocator(
          allocator,
          [self = shared_from_this(), buffer, op] { self->async_read_some_internal(buffer, op); }));
}

void socket::impl::install_transfer_cancellation(const transfer_op::ptr& op)
{
  auto cancellation_slot = boost::asio::get_associated_cancellation_slot(op->m_handler);
  if (!cancellation_slot.is_connected()) {
    return;
  }
  const std::weak_ptr<impl> weak_self      = shared_from_this();
  const std::weak_ptr<transfer_op> weak_op = op;
  cancellation_slot.assign([weak_self, weak_op](boost::asio::cancellation_type cancellation_type) {
    if (cancellation_type == boost::asio::cancellation_type::none) {
      return;
    }
    const auto op = weak_op.lock();
    if (!op) {
      return;
    }
    op->m_cancelled.store(true, std::memory_order_release);
    const auto self = weak_self.lock();
    if (self) {
      boost::asio::dispatch(self->m_strand, [self, op] { self->cancel_transfer(op); });
    }
  });
}

void socket::impl::async_connect_internal(type type, const endpoint& endpoint, const connect_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_connect(op, boost::asio::error::operation_aborted);
    return;
  }
  if (m_state != state::null) {
    complete_connect(op, error::code::invalid_state);
  }
  else if (endpoint.m_server_address_from_uri_param && !m_settings.m_config.m_no_token && !m_settings.m_config.m_token_from_uri_param) {
    complete_connect(op, error::code::invalid_configuration);
  }
  else if (!endpoint.m_id_name) {
    complete_connect(op, error::code::invalid_endpoint);
  }
  else {
    m_type       = type;
    m_endpoint   = endpoint;
    m_connect_op = op;
    m_state      = state::connecting;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_is_state_non_null = true;
    }
    do_resolve_host();
  }
}

void socket::impl::async_write_some_internal(const boost::asio::const_buffer& buffer, const transfer_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_transfer(op, boost::asio::error::operation_aborted, 0);
    return;
  }
  if (m_state == state::connected) {
    op->m_started           = true;
    auto completion_handler = [self = shared_from_this(), op](const boost::system::error_code& error_code, std::size_t transferred) {
      self->on_transfer(op, error_code, transferred);
    };
    m_socket.async_write_some(
        buffer,
        core::bind_handler_allocator(
            core::allocator::wrapper<char>(m_allocator),
            boost::asio::bind_cancellation_slot(op->m_cancellation.slot(), boost::asio::bind_executor(m_strand, std::move(completion_handler)))));
  }
  else {
    complete_transfer(op, error::code::invalid_state, 0);
  }
}

void socket::impl::async_write_some_internal(const const_buffer_sequence_type& buffer, const transfer_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_transfer(op, boost::asio::error::operation_aborted, 0);
    return;
  }
  if (m_state == state::connected) {
    op->m_started           = true;
    auto completion_handler = [self = shared_from_this(), op](const boost::system::error_code& error_code, std::size_t transferred) {
      self->on_transfer(op, error_code, transferred);
    };
    m_socket.async_write_some(
        buffer,
        core::bind_handler_allocator(
            core::allocator::wrapper<char>(m_allocator),
            boost::asio::bind_cancellation_slot(op->m_cancellation.slot(), boost::asio::bind_executor(m_strand, std::move(completion_handler)))));
  }
  else {
    complete_transfer(op, error::code::invalid_state, 0);
  }
}

void socket::impl::async_read_some_internal(const boost::asio::mutable_buffer& buffer, const transfer_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_transfer(op, boost::asio::error::operation_aborted, 0);
    return;
  }
  if (m_state == state::connected) {
    op->m_started           = true;
    auto completion_handler = [self = shared_from_this(), op](const boost::system::error_code& error_code, std::size_t transferred) {
      self->on_transfer(op, error_code, transferred);
    };
    m_socket.async_read_some(
        buffer,
        core::bind_handler_allocator(
            core::allocator::wrapper<char>(m_allocator),
            boost::asio::bind_cancellation_slot(op->m_cancellation.slot(), boost::asio::bind_executor(m_strand, std::move(completion_handler)))));
  }
  else {
    complete_transfer(op, error::code::invalid_state, 0);
  }
}

void socket::impl::async_read_some_internal(const mutable_buffer_sequence_type& buffer, const transfer_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_transfer(op, boost::asio::error::operation_aborted, 0);
    return;
  }
  if (m_state == state::connected) {
    op->m_started           = true;
    auto completion_handler = [self = shared_from_this(), op](const boost::system::error_code& error_code, std::size_t transferred) {
      self->on_transfer(op, error_code, transferred);
    };
    m_socket.async_read_some(
        buffer,
        core::bind_handler_allocator(
            core::allocator::wrapper<char>(m_allocator),
            boost::asio::bind_cancellation_slot(op->m_cancellation.slot(), boost::asio::bind_executor(m_strand, std::move(completion_handler)))));
  }
  else {
    complete_transfer(op, error::code::invalid_state, 0);
  }
}

void socket::impl::cancel_connect(const connect_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!op || !op->m_cancelled.load(std::memory_order_acquire) || op->m_completed) {
    return;
  }
  if (m_connect_op == op) {
    on_close(boost::asio::error::operation_aborted);
  }
  else {
    complete_connect(op, boost::asio::error::operation_aborted);
  }
}

void socket::impl::cancel_transfer(const transfer_op::ptr& op)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!op || !op->m_cancelled.load(std::memory_order_acquire) || op->m_completed) {
    return;
  }
  if (op->m_started) {
    op->m_cancellation.emit(boost::asio::cancellation_type::all);
  }
  else {
    complete_transfer(op, boost::asio::error::operation_aborted, 0);
  }
}

void socket::impl::complete_connect(const connect_op::ptr& op, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!op || op->m_completed) {
    return;
  }
  op->m_completed = true;
  rstream::core::invoke_completion_handler(m_executor, std::move(op->m_handler), error_code);
  op->m_handler = nullptr;
  if (m_connect_op == op) {
    m_connect_op = nullptr;
  }
}

void socket::impl::complete_transfer(const transfer_op::ptr& op, const boost::system::error_code& error_code, std::size_t transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!op || op->m_completed) {
    return;
  }
  op->m_completed = true;
  rstream::core::invoke_completion_handler(m_executor, std::move(op->m_handler), error_code, transferred);
  op->m_handler = nullptr;
}

void socket::impl::on_transfer(const transfer_op::ptr& op, const boost::system::error_code& error_code, std::size_t transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  op->m_started = false;
  if (op->m_cancelled.load(std::memory_order_acquire)) {
    complete_transfer(op, boost::asio::error::operation_aborted, transferred);
  }
  else {
    complete_transfer(op, error_code, transferred);
  }
}

void socket::impl::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  const auto op           = m_connect_op;
  op->m_started           = true;
  auto completion_handler = [self = shared_from_this(), op](
                                const boost::system::error_code& error_code,
                                const resolver_type::results_type& results) {
    self->on_do_resolve_host(op, error_code, results);
  };
  auto internal_handler = boost::asio::bind_executor(
      m_strand,
      core::bind_handler_allocator(
          core::allocator::wrapper<char>(m_allocator),
          boost::asio::bind_cancellation_slot(boost::asio::cancellation_slot(), std::move(completion_handler))));
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_endpoint.m_server_address.m_url, std::move(internal_handler));
#else
  m_resolver.async_resolve(m_endpoint.m_server_address.host(), m_endpoint.m_server_address.port(), std::move(internal_handler));
#endif
}

void socket::impl::on_do_resolve_host(const connect_op::ptr& op, const boost::system::error_code& error_code, const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  op->m_started = false;
  if (op->m_terminal_error) {
    on_error(op->m_terminal_error);
    return;
  }
  if (m_state != state::connecting || m_connect_op != op) {
    complete_connect(op, boost::asio::error::operation_aborted);
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
  const auto op           = m_connect_op;
  op->m_started           = true;
  auto completion_handler = [self = shared_from_this(), op](
                                const boost::system::error_code& error_code,
                                const resolver_type::results_type::endpoint_type& endpoint) {
    self->on_connect(op, error_code, endpoint);
  };
  auto internal_handler = boost::asio::bind_executor(
      m_strand,
      core::bind_handler_allocator(
          core::allocator::wrapper<char>(m_allocator),
          boost::asio::bind_cancellation_slot(boost::asio::cancellation_slot(), std::move(completion_handler))));
  boost::asio::async_connect(m_socket, results, std::move(internal_handler));
}

void socket::impl::on_connect(const connect_op::ptr& op, const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type&)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  op->m_started = false;
  if (op->m_terminal_error) {
    on_error(op->m_terminal_error);
    return;
  }
  if (m_state != state::connecting || m_connect_op != op) {
    complete_connect(op, boost::asio::error::operation_aborted);
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
  const auto op           = m_connect_op;
  op->m_started           = true;
  auto completion_handler = [self = shared_from_this(), op](const boost::system::error_code& error_code) {
    self->on_handshake(op, error_code);
  };
  auto internal_handler = boost::asio::bind_executor(
      m_strand,
      core::bind_handler_allocator(
          core::allocator::wrapper<char>(m_allocator),
          boost::asio::bind_cancellation_slot(boost::asio::cancellation_slot(), std::move(completion_handler))));
  const auto type = m_type == type::client ? handshake_type::type::stream_req : handshake_type::type::proxy_req;
  auto handshake  = std::allocate_shared<handshake_type>(core::allocator::wrapper<impl>(m_allocator), m_socket, m_endpoint.m_server_address, m_settings.m_config, m_allocator);
  handshake->async_run(type, m_endpoint.m_id_name.get(), m_endpoint.m_secret, std::move(internal_handler));
}

void socket::impl::on_handshake(const connect_op::ptr& op, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  op->m_started = false;
  if (op->m_terminal_error) {
    on_error(op->m_terminal_error);
    return;
  }
  if (m_state != state::connecting || m_connect_op != op) {
    complete_connect(op, boost::asio::error::operation_aborted);
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
  complete_connect(m_connect_op, boost::system::error_code());
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
  auto connect_error = error_code;
  if (!connect_error && m_connect_op) {
    connect_error = boost::asio::error::operation_aborted;
  }
  const bool defer_connect_completion = m_connect_op && m_connect_op->m_started;
  if (defer_connect_completion) {
    m_connect_op->m_terminal_error = connect_error;
  }
  else {
    m_state = state::null;
    complete_connect(m_connect_op, connect_error);
  }
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
