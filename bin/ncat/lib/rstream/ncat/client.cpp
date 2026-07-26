// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#ifdef _WIN32
#include <rstream/core/windows/blocking_handle.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>

#include <unistd.h>
#endif

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>
#endif

#include "error.hpp"

// clang-format off
// To be included after boost headers
#ifdef _WIN32
#include <windows.h>
#endif
// clang-format on

namespace rstream {
namespace ncat {

class RSTREAM_GNUC_INTERNAL client::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_client& settings);

  virtual ~impl() = default;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = rstream::io::stream;
#else
  using protocol_type = boost::asio::ip::tcp;
#endif

  using resolver_type = protocol_type::resolver;

  using socket_type = protocol_type::socket;

#ifdef _WIN32
  using stream_type = rstream::core::windows::blocking_handle;
#else
  using stream_type = boost::asio::posix::stream_descriptor;
#endif

  enum class state {
    null          = 0,
    connecting    = 1,
    connected     = 2,
    disconnecting = 3,
    disconnected  = 4
  };

  void set_state(state state);

  void async_run_internal(async_run_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_connect(const resolver_type::results_type& endpoints);

  void on_connect(const boost::system::error_code& error_code);

  void on_connected();

  void run_loop();

  void do_read_socket();

  void on_read_socket(const boost::system::error_code& error_code, std::size_t size);

  void do_write_stdout();

  void on_write_stdout(const boost::system::error_code& error_code, std::size_t size);

  void do_read_std_in();

  void on_read_std_in(const boost::system::error_code& error_code, std::size_t size);

  void do_write_socket();

  void on_write_socket(const boost::system::error_code& error_code, std::size_t size);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const config m_config;

  const settings_client m_settings;

  core::logger m_logger;

  state m_state;

  async_run_completion_handler m_handler;

  resolver_type m_resolver;

  socket_type m_socket;

  stream_type m_stream_std_in;

  stream_type m_stream_std_out;

  core::buffer m_buffer_std_in;

  core::buffer m_buffer_socket;

  boost::system::error_code m_error_code;

  bool m_interactive;

  bool m_std_in_eos;
};

client::client(const executor_type& executor, const config& config, const settings_client& settings)
    : io::io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

client::~client() noexcept
{
  try {
    m_impl->cancel();
  }
  catch (...) {
    return;
  }
}

void client::async_run(async_run_completion_handler&& handler)
{
  m_impl->async_run(std::forward<decltype(handler)>(handler));
}

void client::cancel()
{
  m_impl->cancel();
}

client::impl::impl(const executor_type& executor, const config& config, const settings_client& settings)
    : m_executor(executor),
      m_strand(executor),
      m_config(config),
      m_settings(settings),
      m_logger({"rstream", "ncat", "client", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null),
      m_resolver(executor),
      m_socket(executor),
#ifdef _WIN32
      m_stream_std_in(executor),
      m_stream_std_out(executor),
#else
      m_stream_std_in(executor, ::dup(STDIN_FILENO)),
      m_stream_std_out(executor, ::dup(STDOUT_FILENO)),
#endif
      m_buffer_std_in(rstream::core::make_buffer_allocated(m_settings.m_read_std_in_buffer_size_bytes)),
      m_buffer_socket(rstream::core::make_buffer_allocated(m_settings.m_read_socket_buffer_size_bytes)),
      m_interactive(false),
      m_std_in_eos(false)
{
  m_interactive = m_config.m_interactive;
  if (!m_config.m_interactive && !m_config.m_non_interactive) {
    m_interactive = true;
  }
#ifdef _WIN32
  boost::system::error_code error_code;
  m_stream_std_out.open(::GetStdHandle(STD_OUTPUT_HANDLE), stream_type::access::write, error_code);
  if (!error_code && m_interactive) {
    m_stream_std_in.open(::GetStdHandle(STD_INPUT_HANDLE), stream_type::access::read, error_code);
  }
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
#endif
}

void client::impl::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), std::move(handler)));
}

void client::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&client::impl::cancel_internal, shared_from_this(), error::code::operation_aborted));
}

void client::impl::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
  std::stringstream str;
  switch (state) {
    case state::connecting:
      str << "connecting";
      break;
    case state::connected:
      str << "connected";
      break;
    case state::disconnecting:
      str << "disconnecting";
      break;
    case state::disconnected:
      str << "disconnected";
      break;
    default:
      break;
  }
  if (!str.str().empty()) {
    m_logger->debug("client state changed to '{}'", str.str());
  }
}

void client::impl::async_run_internal(async_run_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::null) {
    if (handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state);
    }
    return;
  }
  m_handler.swap(handler);
  set_state(state::connecting);
  do_resolve_host();
}

void client::impl::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_config.m_address.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_config.m_address.host(), m_config.m_address.port(), boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void client::impl::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
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

void client::impl::do_connect(const resolver_type::results_type& endpoints)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_connect, shared_from_this(), std::placeholders::_1);
  boost::asio::async_connect(m_socket, endpoints, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_connect(const boost::system::error_code& error_code)
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
    on_connected();
  }
}

void client::impl::on_connected()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  set_state(state::connected);
  std::stringstream str;
  {
    boost::system::error_code error;
    str << m_socket.remote_endpoint(error);
    if (!error) {
      m_logger->debug("connected to [{}]", str.str());
    }
  }
  run_loop();
}

void client::impl::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_socket();
  if (m_interactive) {
    do_read_std_in();
  }
}

void client::impl::do_read_socket()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  m_buffer_socket.reset_size();
  auto completion_handler = std::bind(&impl::on_read_socket, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_socket.async_read_some(core::helpers::mutable_memory_sequence(m_buffer_socket), boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_read_socket(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (error_code) {
    if (core::helpers::is_eof_error(error_code)) {
      on_close(boost::system::error_code());
    }
    else {
      on_error(error_code);
    }
  }
  else {
    m_buffer_socket.set_size(size);
    do_write_stdout();
  }
}

void client::impl::do_write_stdout()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  auto completion_handler = std::bind(&impl::on_write_stdout, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef _WIN32
  m_stream_std_out.async_write(boost::asio::const_buffer(m_buffer_socket.map().get_const_data(), m_buffer_socket.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
#else
  boost::asio::async_write(m_stream_std_out, core::helpers::const_memory_sequence(m_buffer_socket), boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void client::impl::on_write_stdout(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)size;
  if (m_state != state::connected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_socket();
  }
}

void client::impl::do_read_std_in()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected || m_std_in_eos) {
    return;
  }
  m_buffer_std_in.reset_size();
  auto completion_handler = std::bind(&impl::on_read_std_in, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_stream_std_in.async_read_some(boost::asio::mutable_buffer(m_buffer_std_in.map().get_data(), m_buffer_std_in.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_read_std_in(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  bool eos = false;
  if (error_code) {
    eos = core::helpers::is_eof_error(error_code);
  }
  if (error_code && !eos) {
    on_error(error_code);
  }
  else if (eos) {
    m_std_in_eos = true;
#ifndef RSTREAM_WITH_IO_STREAMS
    boost::system::error_code tmp;
    m_socket.shutdown(boost::asio::socket_base::shutdown_send, tmp);
#endif
  }
  else {
    m_buffer_std_in.set_size(size);
    do_write_socket();
  }
}

void client::impl::do_write_socket()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  auto completion_handler = std::bind(&impl::on_write_socket, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::asio::async_write(m_socket, boost::asio::buffer(m_buffer_std_in.map().get_const_data(), m_buffer_std_in.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_write_socket(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)size;
  if (m_state != state::connected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_std_in();
  }
}

void client::impl::cancel_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = error_code ? error_code : error::code::operation_aborted;
  do_close(cause);
}

void client::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  do_close(error_code);
}

void client::impl::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::disconnecting || m_state == state::disconnected) {
    return;
  }
  if (error_code && !m_error_code) {
    m_error_code = error_code;
  }
  set_state(state::disconnecting);
  on_close(error_code);
}

void client::impl::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::disconnected) {
    return;
  }
  set_state(state::disconnected);
  auto cause = m_error_code ? m_error_code : error_code;
  {
    boost::system::error_code tmp;
    m_resolver.cancel();
    m_socket.close(tmp);
#ifdef _WIN32
    m_stream_std_in.close();
    m_stream_std_out.close();
#else
    m_stream_std_in.close(tmp);
    m_stream_std_out.close(tmp);
#endif
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler = nullptr;
}

}  // namespace ncat
}  // namespace rstream
