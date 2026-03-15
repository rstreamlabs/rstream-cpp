// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <map>
#include <set>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#ifdef _WIN32
#include <boost/asio/windows/stream_handle.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/signal_set.hpp>

#include <unistd.h>
#endif
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/buffers_adaptor.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/core/memory.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#endif
#include <rstream/io/payloader.hpp>
#include <rstream/io/queue.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/websocket.hpp>
#include <rstream/io/stream.hpp>
#endif
#include <rstream/rtty/protobuf/messages.pb.h>

#include "detail/convert.hpp"
#include "error.hpp"
#include "terminal.hpp"

// clang-format off
// To be included after boost headers
#ifdef _WIN32
#include <windows.h>
#endif
// clang-format on

namespace rstream {
namespace rtty {

namespace {

bool is_eof_error(const std::error_code& error_code)
{
  if (!error_code) {
    return false;
  }
  if (error_code == boost::system::error_code(boost::asio::error::eof)) {
    return true;
  }
#ifdef _WIN32
  if (error_code == boost::system::error_code(boost::asio::error::broken_pipe)) {
    return true;
  }
  if (error_code.value() == ERROR_BROKEN_PIPE && error_code.category() == std::system_category()) {
    return true;
  }
#endif
  return false;
}

#ifdef _WIN32
HANDLE duplicate_handle(HANDLE handle)
{
  HANDLE dup = nullptr;
  if (!handle || handle == INVALID_HANDLE_VALUE) {
    return nullptr;
  }
  if (!::DuplicateHandle(::GetCurrentProcess(), handle, ::GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
    return nullptr;
  }
  return dup;
}
#endif

bool is_same_terminal_size(const terminal_size& lhs, const terminal_size& rhs)
{
  return lhs.m_row == rhs.m_row && lhs.m_col == rhs.m_col && lhs.m_xpixel == rhs.m_xpixel && lhs.m_ypixel == rhs.m_ypixel;
}

}  // namespace

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

  using websocket_type = std::shared_ptr<boost::beast::websocket::stream<socket_type&, false>>;

  using payloader_type = std::shared_ptr<rstream::io::payloader<socket_type&>>;

  using queue_type = rstream::io::queue_base::ptr;

#ifdef _WIN32
  using stream_type = boost::asio::windows::stream_handle;
#else
  using stream_type     = boost::asio::posix::stream_descriptor;
  using signal_set_type = boost::asio::signal_set;
#endif

  enum class stdfd_type {
    std_out,
    std_err
  };

  enum class loop {
    null,
    read_std_in,
    read_terminal_size,
    heartbeat
  };

  enum class state {
    null          = 0,
    connecting    = 1,
    connected     = 2,
    disconnecting = 3,
    disconnected  = 4
  };

  using state_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms);

  void async_run_internal(async_run_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const std::error_code& error_code, const resolver_type::results_type& results);

  void do_connect(const resolver_type::results_type& results);

  void on_connect(const std::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint);

  void do_handshake_websocket(const resolver_type::results_type::endpoint_type& endpoint);

  void on_handshake_websocket(const std::error_code& error_code);

  void do_open();

  void on_open();

  void cancel_internal();

  void on_error(const std::error_code& error_code);

  void do_close(const std::error_code& error_code);

  void on_close(const std::error_code& error_code, int code = 0);

  void run_loop();

  void on_cmd_complete(int code);

  void do_close_websocket(int code);

  void on_close_websocket(const std::error_code& error_code, int code);

  void read_std_in_loop();

  void do_read_std_in();

  void on_read_std_in(const std::error_code& error_code, std::size_t size);

  void read_terminal_size_loop();

  void do_wait_for_terminal_size();

  void do_send_message(const rstream::rtty::protobuf::Message& message, enum loop loop = loop::null);

  void on_send_message(const std::error_code& error_code, enum loop loop);

  void on_terminal_size_signal(const std::error_code& error_code, int);

  void on_wait_for_terminal_size(const std::error_code& error_code);

  void on_terminal_size(const terminal_size& terminal_size);

  void process_incoming_messages_loop();

  void do_read_incoming_message();

  void on_read_incoming_data(const std::error_code& error_code);

  void on_read_incoming_message(const rstream::rtty::protobuf::Message& message);

  void do_process_data(const rstream::rtty::protobuf::Data& data);

  void do_process_data(const std::shared_ptr<std::string>& buffer, stdfd_type type);

  void on_process_data(const std::error_code& error_code);

  void send_heartbeat();

  void do_send_heartbeat();

  static bool is_message_expected(state state, const rstream::rtty::protobuf::Message& message);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const config m_config;

  const settings_client m_settings;

  state m_state;

  async_run_completion_handler m_handler;

  resolver_type m_resolver;

  socket_type m_socket;

  websocket_type m_websocket;

  payloader_type m_payloader;

  queue_type m_queue;

  stream_type m_stream_std_in;

  stream_type m_stream_std_out;

  stream_type m_stream_std_err;

  boost::asio::deadline_timer m_terminal_size_timer;

#ifndef _WIN32
  signal_set_type m_signal_set;
#endif

  rstream::core::buffer m_buffer_std_in;

  rstream::core::buffer m_buffer_socket;

  boost::beast::buffers_adaptor<core::helpers::mutable_memory_sequence> m_http_buffers_adaptor;

  std::shared_ptr<terminal> m_terminal_std_in;

  std::error_code m_error_code;

  boost::optional<terminal_size> m_terminal_size;

  state_changed_signal_type m_state_changed_signal;
};

client::client(const executor_type& executor, const config& config, const settings_client& settings)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

client::~client()
{
  m_impl->cancel();
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
      m_state(state::null),
      m_resolver(executor),
      m_socket(executor),
#ifdef _WIN32
      m_stream_std_in(executor, duplicate_handle(::GetStdHandle(STD_INPUT_HANDLE))),
      m_stream_std_out(executor, duplicate_handle(::GetStdHandle(STD_OUTPUT_HANDLE))),
      m_stream_std_err(executor, duplicate_handle(::GetStdHandle(STD_ERROR_HANDLE))),
#else
      m_stream_std_in(executor, ::dup(STDIN_FILENO)),
      m_stream_std_out(executor, ::dup(STDOUT_FILENO)),
      m_stream_std_err(executor, ::dup(STDERR_FILENO)),
#endif
      m_terminal_size_timer(executor),
#ifndef _WIN32
      m_signal_set(executor, SIGWINCH),
#endif
      m_buffer_std_in(rstream::core::make_buffer_allocated(m_settings.m_std_in_buffer_size)),
      m_buffer_socket(rstream::core::make_buffer_allocated(m_settings.m_common.m_mtu)),
      m_http_buffers_adaptor(core::helpers::mutable_memory_sequence(m_buffer_socket))
{
  if (config.m_protocol_config.m_protocol_type == protocol::type::websocket) {
    m_websocket = std::make_shared<websocket_type::element_type>(m_socket);
  }
  else if (config.m_protocol_config.m_protocol_type == protocol::type::plain) {
    m_payloader = std::make_shared<payloader_type::element_type>(m_socket);
  }
  if (m_websocket) {
    m_queue = std::make_shared<rstream::io::queue<websocket_type::element_type&>>(*m_websocket);
  }
  else {
    m_queue = std::make_shared<rstream::io::queue<payloader_type::element_type&>>(*m_payloader);
  }
  if (m_config.m_protocol_config.m_options.m_allocate_tty) {
#ifdef _WIN32
    m_terminal_std_in = std::make_shared<terminal>(::GetStdHandle(STD_INPUT_HANDLE));
#else
    m_terminal_std_in = std::make_shared<terminal>(STDIN_FILENO);
#endif
  }
}

void client::impl::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), std::move(handler)));
}

void client::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&client::impl::cancel_internal, shared_from_this()));
}

void client::impl::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
  m_state_changed_signal(state);
}

void client::impl::arm_state_timer(unsigned int timeout_ms)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (timeout_ms == 0) {
    return;
  }
  struct task : std::enable_shared_from_this<task> {
    task(const executor_type& executor)
        : m_complete(false),
          m_timer(executor)
    {
    }
    bool m_complete;
    boost::asio::deadline_timer m_timer;
    boost::signals2::scoped_connection m_signal_state;
  };
  auto ptr         = shared_from_this();
  auto task_ptr    = std::make_shared<struct task>(m_executor);
  auto on_state_cb = [task_ptr, ptr](state) {
    if (task_ptr->m_complete) {
      return;
    }
    task_ptr->m_complete = true;
    task_ptr->m_timer.cancel();
    task_ptr->m_signal_state = boost::signals2::scoped_connection();
  };
  task_ptr->m_signal_state = m_state_changed_signal.connect(on_state_cb);
  auto on_timer_cb         = [task_ptr, ptr](const std::error_code& error_code) {
    if (task_ptr->m_complete) {
      return;
    }
    if (!error_code) {
      ptr->on_error(error::code::operation_timeout);
    }
  };
  task_ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
  auto completion_handler = boost::asio::bind_executor(ptr->m_strand, on_timer_cb);
  task_ptr->m_timer.async_wait(completion_handler);
}

void client::impl::async_run_internal(async_run_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::null) {
    if (handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, -1);
    }
    return;
  }
  m_handler.swap(handler);
  set_state(state::connecting);
  arm_state_timer(m_settings.m_common.m_timeouts_ms.m_open);
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

void client::impl::on_do_resolve_host(const std::error_code& error_code, const resolver_type::results_type& results)
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
    do_connect(results);
  }
}

void client::impl::do_connect(const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_connect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::asio::async_connect(m_socket, results, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_connect(const std::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint)
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
    if (m_websocket) {
      do_handshake_websocket(endpoint);
    }
    else {
      do_open();
    }
  }
}

void client::impl::do_handshake_websocket(const resolver_type::results_type::endpoint_type& endpoint)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  // we cannot process messages bigger than our buffer
  m_websocket->read_message_max(m_buffer_socket.get_size());
  m_websocket->write_buffer_bytes(m_buffer_socket.get_size());
  // we're sending binary data
  m_websocket->binary(true);
  // user fast cache algorithm
  m_websocket->secure_prng(false);
  // set timeouts settings for the websocket
  m_websocket->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
  // set a decorator to change the 'User-Agent' of the handshake
  auto decorator = [](boost::beast::websocket::request_type& request) {
    request.set(boost::beast::http::field::user_agent, "rstream websocket-client-async");
  };
  m_websocket->set_option(boost::beast::websocket::stream_base::decorator(decorator));
  // Perform the websocket handshake
  auto completion_handler = std::bind(&impl::on_handshake_websocket, shared_from_this(), std::placeholders::_1);
  auto target             = m_config.m_websocket_target ? *m_config.m_websocket_target : "/";
  m_websocket->async_handshake(m_config.m_address.host(), target, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_handshake_websocket(const std::error_code& error_code)
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
    do_open();
  }
}

void client::impl::do_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  rstream::rtty::protobuf::Message message;
  auto config = m_config.m_protocol_config;
  if (config.m_options.m_allocate_tty) {
    protocol::add_environment_variable(config.m_env_vars, "TERM");
  }
  detail::convert(*message.mutable_open()->mutable_config(), config);
  do_read_incoming_message();
  do_send_message(message);
}

void client::impl::on_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  set_state(state::connected);
  run_loop();
}

void client::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = error::code::operation_aborted;
  if (m_state == state::connected) {
    do_close(cause);
  }
  else if (m_state == state::connecting) {
    on_error(cause);
  }
}

void client::impl::on_error(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void client::impl::do_close(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected || !error_code) {
    return;
  }
  set_state(state::disconnecting);
  m_error_code = error_code;
  rstream::rtty::protobuf::Message message;
  error::code code;
  if (error_code.category() == std::error_code((error::code){}).category()) {
    code = (error::code)error_code.value();
  }
  else {
    code = error::code::unknown_undefined_error;
  }
  message.mutable_error()->set_msg(error_code.message());
  arm_state_timer(m_settings.m_common.m_timeouts_ms.m_close);
  do_send_message(message);
}

void client::impl::on_close(const std::error_code& error_code, int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (m_state == state::connected || m_state == state::disconnecting) {
    if (m_terminal_std_in) {
      std::error_code tmp;
      m_terminal_std_in->reset(tmp);
    }
  }
  set_state(state::disconnected);
  auto cause = error_code;
  if (cause && m_error_code) {
    cause = m_error_code;
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause, cause ? -1 : code);
  }
  {
    boost::system::error_code tmp;
    m_resolver.cancel();
    m_socket.close(tmp);
    m_queue->cancel();
    m_stream_std_in.close(tmp);
    m_stream_std_out.close(tmp);
    m_stream_std_err.close(tmp);
    m_terminal_size_timer.cancel(tmp);
#ifndef _WIN32
    m_signal_set.cancel(tmp);
#endif
  }
}

void client::impl::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_config.m_protocol_config.m_options.m_interactive) {
    read_std_in_loop();
  }
  if (m_config.m_protocol_config.m_options.m_allocate_tty) {
    read_terminal_size_loop();
  }
  process_incoming_messages_loop();
  if (m_config.m_protocol_config.m_options.m_send_heartbeat) {
    send_heartbeat();
  }
}

void client::impl::on_cmd_complete(int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_websocket) {
    do_close_websocket(code);
  }
  else {
    on_close(std::error_code(), code);
  }
}

void client::impl::do_close_websocket(int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_close_websocket, shared_from_this(), std::placeholders::_1, code);
  m_websocket->async_close(boost::beast::websocket::close_code::normal, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_close_websocket(const std::error_code& error_code, int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  (void)error_code;
  on_close(std::error_code(), code);
}

void client::impl::read_std_in_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  std::error_code error_code;
  if (m_terminal_std_in) {
    m_terminal_std_in->set_raw(error_code);
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_std_in();
  }
}

void client::impl::do_read_std_in()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto completion_handler = std::bind(&impl::on_read_std_in, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_stream_std_in.async_read_some(boost::asio::mutable_buffer(m_buffer_std_in.map().get_data(), m_buffer_std_in.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_read_std_in(const std::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  bool eos = false;
  if (error_code) {
    eos = is_eof_error(error_code);
  }
  if (error_code && !eos) {
    on_error(error_code);
  }
  else {
    rstream::rtty::protobuf::Message message;
    auto data = message.mutable_data();
    data->set_type(rstream::rtty::protobuf::Data_Type_TYPE_STDIN);
    if (eos) {
      data->mutable_eos();
    }
    else {
      data->set_data(m_buffer_std_in.map().get_const_data(), size);
    }
    do_send_message(message, eos ? loop::null : loop::read_std_in);
  }
}

void client::impl::read_terminal_size_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  on_terminal_size_signal(std::error_code(), 0);
}

void client::impl::do_wait_for_terminal_size()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
#ifdef _WIN32
  m_terminal_size_timer.expires_from_now(boost::posix_time::milliseconds(300));
  auto completion_handler = std::bind(&impl::on_wait_for_terminal_size, shared_from_this(), std::placeholders::_1);
  m_terminal_size_timer.async_wait(boost::asio::bind_executor(m_strand, completion_handler));
#else
  auto completion_handler = std::bind(&impl::on_terminal_size_signal, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_signal_set.async_wait(boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void client::impl::on_wait_for_terminal_size(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  on_terminal_size_signal(error_code, 0);
}

void client::impl::on_terminal_size_signal(const std::error_code& error_code, int)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = error_code;
  terminal_size size;
  if (!cause) {
    try {
      size = m_terminal_std_in->get_size(cause);
    }
    catch (std::system_error& error) {
      cause = error.code();
    }
    catch (...) {
      cause = error::code::unknown_undefined_error;
    }
  }
  if (!cause) {
    if (m_terminal_size && is_same_terminal_size(m_terminal_size.get(), size)) {
      do_wait_for_terminal_size();
      return;
    }
    m_terminal_size = size;
    on_terminal_size(size);
  }
  else {
    on_error(cause);
  }
}

void client::impl::on_terminal_size(const terminal_size& terminal_size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  rstream::rtty::protobuf::Message message;
  detail::convert(*message.mutable_parameter()->mutable_terminal_size(), terminal_size);
  do_send_message(message, loop::read_terminal_size);
}

void client::impl::do_send_message(const rstream::rtty::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  std::size_t buffer_size = message.ByteSizeLong();
  auto buffer             = rstream::core::make_buffer_allocated(buffer_size);
  message.SerializeToArray(buffer.map().get_data(), buffer_size);
  auto completion_handler = std::bind(&impl::on_send_message, shared_from_this(), std::placeholders::_1, loop);
  m_queue->async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_send_message(const std::error_code& error_code, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    switch (loop) {
      case loop::read_std_in:
        do_read_std_in();
        break;
      case loop::read_terminal_size:
        do_wait_for_terminal_size();
        break;
      case loop::heartbeat:
        do_send_heartbeat();
        break;
      default:
        break;
    }
  }
}

void client::impl::process_incoming_messages_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_incoming_message();
}

void client::impl::do_read_incoming_message()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  m_buffer_socket.reset_size();
  auto completion_handler = std::bind(&impl::on_read_incoming_data, shared_from_this(), std::placeholders::_1);
  if (m_websocket) {
    auto handler = [this, completion_handler](const std::error_code& error_code, std::size_t bytes_transferred) {
      m_buffer_socket.set_size(bytes_transferred);
      completion_handler(error_code);
    };
    m_http_buffers_adaptor = boost::beast::buffers_adaptor<core::helpers::mutable_memory_sequence>(core::helpers::mutable_memory_sequence(m_buffer_socket));
    m_websocket->async_read(m_http_buffers_adaptor, boost::asio::bind_executor(m_strand, handler));
  }
  else {
    m_payloader->async_recv(m_buffer_socket, boost::asio::bind_executor(m_strand, completion_handler));
  }
}

void client::impl::on_read_incoming_data(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    rstream::rtty::protobuf::Message message;
    if (message.ParseFromArray(m_buffer_socket.map().get_const_data(), m_buffer_socket.get_size())) {
      on_read_incoming_message(message);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

void client::impl::on_read_incoming_message(const rstream::rtty::protobuf::Message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  bool do_read_messages = false;
  std::error_code error_code;
  if (!is_message_expected(m_state, message)) {
    error_code = error::code::unexpected_message;
  }
  else {
    using payload_type = rstream::rtty::protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kAck) {
      on_open();
    }
    else if (message_type == payload_type::kError) {
      error_code = error::make_error_code(error::code::server_error);
    }
    else if (message_type == payload_type::kClose) {
      on_cmd_complete(message.close().return_code());
    }
    else if (message_type == payload_type::kData) {
      do_process_data(message.data());
    }
    else if (message_type == payload_type::kHeartbeat) {
      do_read_messages = true;
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else if (do_read_messages) {
    do_read_incoming_message();
  }
}

void client::impl::do_process_data(const rstream::rtty::protobuf::Data& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  stdfd_type type;
  std::error_code error_code;
  if (data.type() == rstream::rtty::protobuf::Data::TYPE_STDOUT) {
    type = stdfd_type::std_out;
  }
  else if (data.type() == rstream::rtty::protobuf::Data::TYPE_STDERR) {
    type = stdfd_type::std_err;
  }
  else {
    error_code = error::code::unexpected_message;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    auto buffer = data.has_eos() ? nullptr : std::make_shared<std::string>(data.data());
    do_process_data(buffer, type);
  }
}

void client::impl::do_process_data(const std::shared_ptr<std::string>& buffer, stdfd_type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto& stream = type == stdfd_type::std_out ? m_stream_std_out : m_stream_std_err;
  if (buffer == nullptr) {
    stream.close();
    on_process_data(std::error_code());
  }
  else {
    auto handler = [ptr = shared_from_this(), buffer](const std::error_code& error_code, std::size_t) {
      ptr->on_process_data(error_code);
    };
    auto completion_handler = std::bind(&impl::on_process_data, shared_from_this(), std::placeholders::_1);
    auto boost_buffer       = boost::asio::const_buffer(buffer->data(), buffer->size());
    boost::asio::async_write(stream, boost_buffer, boost::asio::bind_executor(m_strand, handler));
  }
}

void client::impl::on_process_data(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_incoming_message();
  }
}

void client::impl::send_heartbeat()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_send_heartbeat();
}

void client::impl::do_send_heartbeat()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto timeout_ms = m_settings.m_common.m_timeouts_ms.m_heartbeat;
  if (timeout_ms == 0) {
    return;
  }
  if (m_state != state::connected) {
    return;
  }
  struct task : std::enable_shared_from_this<task> {
    task(const executor_type& executor)
        : m_complete(false),
          m_timer(executor)
    {
    }
    void clean()
    {
      m_complete = true;
      m_timer.cancel();
      m_signal_state = boost::signals2::scoped_connection();
    }
    bool m_complete;
    boost::asio::deadline_timer m_timer;
    boost::signals2::scoped_connection m_signal_state;
  };
  auto ptr         = shared_from_this();
  auto task_ptr    = std::make_shared<struct task>(m_executor);
  auto on_state_cb = [task_ptr, ptr](state) {
    if (task_ptr->m_complete) {
      return;
    }
    task_ptr->clean();
  };
  task_ptr->m_signal_state = m_state_changed_signal.connect(on_state_cb);
  auto on_timer_cb         = [task_ptr, ptr](const std::error_code& error_code) {
    if (task_ptr->m_complete) {
      return;
    }
    if (!error_code) {
      rstream::rtty::protobuf::Message message;
      message.mutable_heartbeat();
      ptr->do_send_message(message, loop::heartbeat);
    }
    task_ptr->clean();
  };
  task_ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
  auto completion_handler = boost::asio::bind_executor(ptr->m_strand, on_timer_cb);
  task_ptr->m_timer.async_wait(completion_handler);
}

bool client::impl::is_message_expected(state state, const rstream::rtty::protobuf::Message& message)
{
  using payload_type                                                                      = rstream::rtty::protobuf::Message::PayloadCase;
  static const std::map<client::impl::state, std::set<payload_type>> compatibility_matrix = {
      {state::connecting, {payload_type::kAck, payload_type::kError}},
      {state::connected, {payload_type::kData, payload_type::kClose, payload_type::kError, payload_type::kHeartbeat}},
      {state::disconnecting, {payload_type::kData, payload_type::kClose, payload_type::kError, payload_type::kHeartbeat}},
  };
  const auto& set = compatibility_matrix.find(state)->second;
  return set.find(message.payload_case()) != set.end();
}

}  // namespace rtty
}  // namespace rstream
