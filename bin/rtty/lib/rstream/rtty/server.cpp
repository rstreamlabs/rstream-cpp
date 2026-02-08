// See LICENSE file in the project root for license information.

#include "server.hpp"

#define BOOST_PROCESS_VERSION 1

#include <map>
#include <set>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/read.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/signal_set.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/buffers_adaptor.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/websocket.hpp>
#if __has_include(<boost/process/v1/args.hpp>)
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/async.hpp>
#include <boost/process/v1/env.hpp>
#include <boost/process/v1/exe.hpp>
#include <boost/process/v1/search_path.hpp>
#include <boost/process/v1/start_dir.hpp>
#else
#include <boost/process/args.hpp>
#include <boost/process/async.hpp>
#include <boost/process/child.hpp>
#include <boost/process/env.hpp>
#include <boost/process/exe.hpp>
#include <boost/process/search_path.hpp>
#include <boost/process/start_dir.hpp>
#endif
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/object_id.hpp>
#include <rstream/io/payloader.hpp>
#include <rstream/io/queue.hpp>
#include <rstream/rtty/protobuf/messages.pb.h>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/websocket.hpp>
#include <rstream/io/stream.hpp>
#endif

#include "detail/convert.hpp"
#include "detail/process.hpp"
#include "error.hpp"
#include "rtty.hpp"
#include "stream.hpp"

namespace rstream {
namespace rtty {

static void parse_environment(boost::process::environment& dst, const protocol::env_vars& src);

class RSTREAM_GNUC_INTERNAL server::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_server& settings);

  virtual ~impl() = default;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class session;

#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = rstream::io::stream;
#else
  using protocol_type = boost::asio::ip::tcp;
#endif

  using endoint_type = protocol_type::endpoint;

  using resolver_type = protocol_type::resolver;

  using acceptor_type = protocol_type::acceptor;

  using socket_type = protocol_type::socket;

  using websocket_type = std::shared_ptr<boost::beast::websocket::stream<socket_type&, false>>;

  using payloader_type = std::shared_ptr<rstream::io::payloader<socket_type&>>;

  using queue_type = rstream::io::queue_base::ptr;

  using session_id_type = std::string;

  using session_ptr_type = std::shared_ptr<session>;

  using sessions_type = std::map<session_id_type, session_ptr_type>;

  enum class state {
    null     = 0,
    starting = 1,
    started  = 2,
    stopping = 3,
    stopped  = 4
  };

  using state_server_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms);

  void async_run_internal(async_run_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const std::error_code& error_code, const resolver_type::results_type& results);

  void on_started();

  void do_accept();

  void on_accept(const std::error_code& error_code);

  void cancel_internal();

  void on_error(const std::error_code& error_code);

  void do_close(const std::error_code& error_code);

  void on_close(const std::error_code& error_code);

  void on_session_closed(const std::error_code& error_code, const session_id_type& session_id);

  static session_id_type generate_session_id();

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const config m_config;

  const settings_server m_settings;

  rstream::core::logger m_logger;

  acceptor_type m_acceptor;

  socket_type m_socket;

  endoint_type m_endpoint;

  state m_state;

  async_run_completion_handler m_handler;

  resolver_type m_resolver;

  sessions_type m_sessions;

  std::error_code m_error_code;

  state_server_changed_signal_type m_state_changed_signal;
};

class RSTREAM_GNUC_INTERNAL server::impl::session : public std::enable_shared_from_this<session> {
 public:
  session(const executor_type& executor, socket_type&& socket, const settings_server& settings, const session_id_type& session_id, protocol::type protocol_type);

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  enum class loop {
    null,
    read_std_out,
    read_std_err,
    heartbeat,
    exit
  };

  enum class state {
    null          = 0,
    connecting    = 1,
    connected     = 2,
    disconnecting = 3,
    disconnected  = 4
  };

  using child_ptr_type = std::shared_ptr<boost::process::child>;

  using state_session_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms);

  void async_run_internal(async_run_completion_handler&& handler);

  void cancel_internal(const std::error_code& error_code);

  void on_error(const std::error_code& error_code);

  void do_close(const std::error_code& error_code);

  void on_close(const std::error_code& error_code);

  void do_read_http_request();

  void on_read_http_request(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_process_http_request();

  void do_write_http_response();

  void on_write_http_response(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_accept_websocket();

  void on_accept_websocket(const std::error_code& error_code);

  void do_open();

  void on_open(const protocol::config& protocol_config);

  void run_loop();

  void read_stdfd_loop();

  void do_read_stdfd(stream::type type);

  void on_read_stdfd(const std::error_code& error_code, std::size_t size, stream::type type);

  void do_send_error(const std::error_code& error_code);

  void do_send_close(int code);

  void do_send_message(const rstream::rtty::protobuf::Message& message, enum loop loop);

  void on_send_message(const std::error_code& error_code, enum loop loop);

  void process_incoming_messages_loop();

  void do_read_incoming_message();

  void on_read_incoming_data(const std::error_code& error_code);

  void on_read_incoming_message(const rstream::rtty::protobuf::Message& message);

  void do_process_data(const rstream::rtty::protobuf::Data& data);

  void do_process_data(const std::shared_ptr<std::string> buffer);

  void on_process_data(const std::error_code& error_code);

  void on_child_exit(const std::error_code& error_code, int code);

  void do_close_websocket();

  void on_close_websocket(const std::error_code& error_code);

  void set_terminal_size(const terminal_size& terminal_size, std::error_code& error_code);

  void send_heartbeat();

  void do_send_heartbeat();

  static bool is_message_expected(state state, const rstream::rtty::protobuf::Message& message);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const settings_server m_settings;

  socket_type m_socket;

  const session_id_type m_session_id;

  rstream::core::logger m_logger;

  websocket_type m_websocket;

  payloader_type m_payloader;

  queue_type m_queue;

  state m_state;

  async_run_completion_handler m_handler;

  rstream::core::buffer m_buffer_socket;

  rstream::core::buffer m_buffer_std_out;

  rstream::core::buffer m_buffer_std_err;

  boost::beast::buffers_adaptor<core::helpers::mutable_memory_sequence> m_http_buffers_adaptor;

  stream::ptr m_stream_ptr;

  child_ptr_type m_child;

  boost::beast::http::request<boost::beast::http::string_body> m_http_request;

  boost::beast::http::response<boost::beast::http::empty_body> m_http_response;

  std::error_code m_error_code;

  state_session_changed_signal_type m_state_changed_signal;

  std::set<stream::type> m_active_streams;

  boost::optional<int> m_return_code;
};

server::server(const executor_type& executor, const config& config, const settings_server& settings)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

server::~server()
{
  cancel();
}

void server::async_run(async_run_completion_handler&& handler)
{
  m_impl->async_run(std::forward<decltype(handler)>(handler));
}

void server::cancel()
{
  m_impl->cancel();
}

server::impl::impl(const executor_type& executor, const config& config, const settings_server& settings)
    : m_executor(executor),
      m_strand(executor),
      m_config(config),
      m_settings(settings),
      m_logger({"rstream", "rtty", "server", fmt::format("#{}", fmt::ptr(this))}),
      m_acceptor(executor),
      m_socket(executor),
      m_state(state::null),
      m_resolver(executor)
{
}

void server::impl::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&server::impl::cancel_internal, shared_from_this()));
}

void server::impl::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
  m_state_changed_signal(state);
  std::stringstream str;
  switch (state) {
    case state::starting:
      str << "starting";
      break;
    case state::started:
      str << "started";
      break;
    case state::stopping:
      str << "stopping";
      break;
    case state::stopped:
      str << "stopped";
      break;
    default:
      break;
  }
  m_logger->debug("server state changed to '{}'", str.str());
}

void server::impl::arm_state_timer(unsigned int timeout_ms)
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

void server::impl::async_run_internal(async_run_completion_handler&& handler)
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
  set_state(state::starting);
  arm_state_timer(m_settings.m_timeouts_start_ms);
  do_resolve_host();
}

void server::impl::do_resolve_host()
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

void server::impl::on_do_resolve_host(const std::error_code& error_code, const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::starting) {
    return;
  }
  auto cause = error_code;
  if (!cause) {
    if (results.empty()) {
      cause = error::code::server_error;
    }
    else {
      boost::system::error_code tmp;
      auto endpoint = results.begin()->endpoint();
#ifdef RSTREAM_WITH_IO_STREAMS
      m_acceptor.open(endpoint, tmp);
#else
      m_acceptor.open(endpoint.protocol(), tmp);
#endif
#ifndef RSTREAM_WITH_IO_STREAMS
      if (!tmp) {
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), tmp);
      }
#endif
      if (!tmp) {
        m_acceptor.bind(endpoint, tmp);
      }
      if (!tmp) {
        m_acceptor.listen(boost::asio::socket_base::max_listen_connections, tmp);
      }
      cause = tmp;
    }
  }
  if (cause) {
    on_error(cause);
  }
  else {
    on_started();
  }
}

void server::impl::on_started()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::starting) {
    return;
  }
  set_state(state::started);
  {
    std::stringstream str;
    boost::system::error_code error;
    str << m_acceptor.local_endpoint(error);
    if (!error) {
      m_logger->info("server is now accepting connections on [{}]", str.str());
    }
  }
  do_accept();
}

void server::impl::do_accept()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::started) {
    return;
  }
  auto completion_handler = std::bind(&impl::on_accept, shared_from_this(), std::placeholders::_1);
  m_acceptor.async_accept(m_socket, m_endpoint, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::on_accept(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::started) {
    return;
  }
  if (error_code) {
    m_logger->warn("an error occured when accepting connection [error_code: {}]", error_code.message());
    on_error(error_code);
  }
  else {
    {
      auto session_id  = generate_session_id();
      auto session_ptr = std::make_shared<session>(m_executor, std::move(m_socket), m_settings, session_id, m_config.m_protocol_type);
      m_sessions.insert(std::make_pair(session_id, session_ptr));
      auto completion_handler = std::bind(&impl::on_session_closed, shared_from_this(), std::placeholders::_1, session_id);
      session_ptr->async_run(boost::asio::bind_executor(m_strand, completion_handler));
#ifdef RSTREAM_WITH_IO_STREAMS
      m_logger->trace("new connection accepted [session_id: {}, {}]", session_id, m_endpoint.to_string());
#else
      {
        std::stringstream str;
        str << m_endpoint;
        m_logger->trace("new connection accepted [session_id: {}, endpoint: {}]", session_id, str.str());
      }
#endif
    }
    do_accept();
  }
}

void server::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::stopped) {
    return;
  }
  if (m_state == state::started) {
    do_close(std::error_code());
  }
  else if (m_state == state::starting) {
    on_close(std::error_code());
  }
}

void server::impl::on_error(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::do_close(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::started) {
    return;
  }
  if (m_sessions.empty()) {
    on_close(error_code);
  }
  else {
    m_logger->debug("stopping pending sessions...");
    if (error_code && !m_error_code) {
      m_error_code = error_code;
    }
    set_state(state::stopping);
    for (const auto& session : m_sessions) {
      session.second->cancel();
    }
  }
}

void server::impl::on_close(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::stopped) {
    return;
  }
  set_state(state::stopped);
  auto cause = m_error_code ? m_error_code : error_code;
  m_logger->debug("server stopped [error_code: {}]", (cause ? cause.message() : "none"));
  for (const auto& session : m_sessions) {
    session.second->cancel();
  }
  m_sessions.clear();
  {
    boost::system::error_code tmp;
    m_acceptor.close(tmp);
    m_resolver.cancel();
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler = nullptr;
}

void server::impl::on_session_closed(const std::error_code& error_code, const session_id_type& session_id)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)error_code;
  m_logger->trace("session closed [session_id: {}, error_code: {}]", session_id, (error_code ? error_code.message() : "none"));
  m_sessions.erase(session_id);
  if (m_state == state::stopping) {
    if (m_sessions.empty()) {
      on_close(std::error_code());
    }
  }
}

server::impl::session_id_type server::impl::generate_session_id()
{
  return rstream::core::object_id();
}

server::impl::session::session(const executor_type& executor, socket_type&& socket, const settings_server& settings, const session_id_type& session_id, protocol::type protocol_type)
    : m_executor(executor),
      m_strand(executor),
      m_settings(settings),
      m_socket(std::move(socket)),
      m_session_id(session_id),
      m_logger({"rstream", "rtty", "session", fmt::format("#{}", session_id)}),
      m_state(state::null),
      m_buffer_socket(rstream::core::make_buffer_allocated(m_settings.m_common.m_mtu)),
      m_buffer_std_out(rstream::core::make_buffer_allocated(m_settings.m_std_out_buffer_size)),
      m_buffer_std_err(rstream::core::make_buffer_allocated(m_settings.m_std_err_buffer_size)),
      m_http_buffers_adaptor(core::helpers::mutable_memory_sequence(m_buffer_socket))
{
  if (protocol_type == protocol::type::websocket) {
    m_websocket = std::make_shared<websocket_type::element_type>(m_socket);
  }
  else if (protocol_type == protocol::type::plain) {
    m_payloader = std::make_shared<payloader_type::element_type>(m_socket);
  }
  if (m_websocket) {
    m_queue = std::make_shared<rstream::io::queue<websocket_type::element_type&>>(*m_websocket);
  }
  else {
    m_queue = std::make_shared<rstream::io::queue<payloader_type::element_type&>>(*m_payloader);
  }
}

void server::impl::session::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::session::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&server::impl::session::cancel_internal, shared_from_this(), std::error_code()));
}

void server::impl::session::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
  m_state_changed_signal(state);
}

void server::impl::session::arm_state_timer(unsigned int timeout_ms)
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

void server::impl::session::async_run_internal(async_run_completion_handler&& handler)
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
  arm_state_timer(m_settings.m_common.m_timeouts_ms.m_open);
  if (m_websocket) {
    do_read_http_request();
  }
  else {
    do_open();
  }
}

void server::impl::session::cancel_internal(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = error_code ? error_code : error::code::operation_aborted;
  if (m_state == state::connected) {
    do_close(cause);
  }
  else if (m_state == state::connecting) {
    on_error(cause);
  }
}

void server::impl::session::on_error(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::session::do_close(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected || !error_code) {
    return;
  }
#ifdef _WIN32
  auto do_kill = [child = m_child](int signal, std::error_code& error_code) {
    if (child->valid() && child->running(error_code)) {
      if (!error_code) {
        ::TerminateProcess(child->native_handle(), signal);
      }
    }
  };
#else
  auto do_kill = [child = m_child](int signal, std::error_code& error_code) {
    if (child->valid() && child->running(error_code)) {
      if (!error_code) {
        ::kill(child->native_handle(), signal);
      }
    }
  };
#endif
  arm_state_timer(m_settings.m_common.m_timeouts_ms.m_close);
  m_logger->debug("sending SIGINT to child");
  std::error_code ec;
#ifdef _WIN32
  do_kill(CTRL_C_EVENT, ec);
#else
  do_kill(SIGINT, ec);
#endif
  if (ec) {
    on_error(ec);
  }
}

void server::impl::session::on_close(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = m_error_code ? m_error_code : error_code;
  set_state(state::disconnected);
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler = nullptr;
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
    m_queue->cancel();
  }
  if (m_stream_ptr) {
    m_stream_ptr->close();
  }
  if (m_child) {
    boost::system::error_code tmp;
    m_child->terminate(tmp);
    m_child->wait(tmp);
  }
  m_child      = nullptr;
  m_stream_ptr = nullptr;
}

void server::impl::session::do_read_http_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer_socket.reset_size();
  m_http_request          = {};
  m_http_buffers_adaptor  = boost::beast::buffers_adaptor<core::helpers::mutable_memory_sequence>(core::helpers::mutable_memory_sequence(m_buffer_socket));
  auto completion_handler = std::bind(&server::impl::session::on_read_http_request, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_read(m_socket, m_http_buffers_adaptor, m_http_request, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_read_http_request(const boost::system::error_code& error_code, std::size_t bytes_transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)bytes_transferred;
  if (error_code == boost::beast::http::error::end_of_stream) {
    on_close(boost::system::error_code());
  }
  else if (error_code) {
    on_error(error_code);
  }
  else {
    do_process_http_request();
  }
}

void server::impl::session::do_process_http_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_http_request.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("HTTP request :\n{}", str.str());
  }
#else
  m_logger->trace("HTTP request '{}' '{}'", std::string(m_http_request.method_string()), std::string(m_http_request.target()));
#endif
  boost::system::error_code error_code;
  auto is_upgrade = false;
  // see if it is a websocket upgrade
  if (boost::beast::websocket::is_upgrade(m_http_request)) {
    if (m_http_request.method() == boost::beast::http::verb::get) {
      is_upgrade = true;
    }
  }
  if (is_upgrade) {
    do_accept_websocket();
  }
  else {
    do_write_http_response();
  }
}

void server::impl::session::do_write_http_response()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_http_response = boost::beast::http::response<boost::beast::http::empty_body>(boost::beast::http::status::bad_request, m_http_request.version());
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_http_response.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("HTTP response :\n{}", str.str());
  }
#else
  m_logger->trace("HTTP response {}", m_http_response.result_int());
#endif
  auto completion_handler = std::bind(&server::impl::session::on_write_http_response, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_write(m_socket, m_http_response, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_write_http_response(const boost::system::error_code& error_code, std::size_t bytes_transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)bytes_transferred;
  if (error_code) {
    on_error(error_code);
  }
  else {
    on_close(boost::system::error_code());
  }
}

void server::impl::session::do_accept_websocket()
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
  // set timeouts settings for the websocket
  m_websocket->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
  // set a decorator to change the 'User-Agent' of the handshake
  auto decorator = [](boost::beast::websocket::request_type& request) {
    request.set(boost::beast::http::field::user_agent, "rstream websocket-server-async");
  };
  m_websocket->set_option(boost::beast::websocket::stream_base::decorator(decorator));
  // accept the websocket handshake
  {
    auto completion_handler = std::bind(&session::on_accept_websocket, shared_from_this(), std::placeholders::_1);
    m_websocket->async_accept(m_http_request, boost::asio::bind_executor(m_strand, completion_handler));
  }
}

void server::impl::session::on_accept_websocket(const std::error_code& error_code)
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

void server::impl::session::do_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  do_read_incoming_message();
}

void server::impl::session::on_open(const protocol::config& protocol_config)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  std::error_code error_code;
  protocol::user_info user_info;
#ifdef _WIN32
  if (protocol_config.m_username) {
    m_logger->warn("changing user is not supported on Windows");
    error_code = error::code::server_error;
  }
  if (!error_code) {
    get_user_info(user_info, error_code);
  }
#else
  get_user_info(user_info, protocol_config.m_username, error_code);
#endif
  if (!error_code) {
    auto workdir = protocol_config.m_workdir ? protocol_config.m_workdir.get() : user_info.m_home;
    boost::process::environment environment;
    auto env_vars_copy = protocol_config.m_env_vars;
    protocol::add_environment_variable(env_vars_copy, "PATH");
#ifdef _WIN32
    protocol::add_environment_variable(env_vars_copy, "ALLUSERSPROFILE");
    protocol::add_environment_variable(env_vars_copy, "COMPUTERNAME");
    protocol::add_environment_variable(env_vars_copy, "COMSPEC");
    protocol::add_environment_variable(env_vars_copy, "CYGWIN");
    protocol::add_environment_variable(env_vars_copy, "OS");
    protocol::add_environment_variable(env_vars_copy, "PATHEXT");
    protocol::add_environment_variable(env_vars_copy, "PROGRAMFILES");
    protocol::add_environment_variable(env_vars_copy, "SYSTEMDRIVE");
    protocol::add_environment_variable(env_vars_copy, "SYSTEMROOT");
    protocol::add_environment_variable(env_vars_copy, "TEMP");
    protocol::add_environment_variable(env_vars_copy, "TMP");
    protocol::add_environment_variable(env_vars_copy, "USERNAME");
    protocol::add_environment_variable(env_vars_copy, "USERPROFILE");
    protocol::add_environment_variable(env_vars_copy, "WINDIR");
#else
    protocol::add_environment_variable(env_vars_copy, "USER", user_info.m_name);
    protocol::add_environment_variable(env_vars_copy, "SHELL", user_info.m_shell);
    protocol::add_environment_variable(env_vars_copy, "HOME", user_info.m_home);
#endif
    parse_environment(environment, env_vars_copy);
    auto completion_handler = [ptr = shared_from_this()](int code, const std::error_code& error_code) {
      boost::asio::post(ptr->m_strand, std::bind_front(&session::on_child_exit, ptr, error_code, code));
    };
    auto backend    = protocol_config.m_options.m_allocate_tty ? stream::backend::tty : stream::backend::pipe;
    const auto exe  = protocol_config.m_cmd_args.size() > 0 ? protocol_config.m_cmd_args.front() : user_info.m_shell;
    const auto args = protocol_config.m_cmd_args.size() > 1 ? protocol::cmd_args(std::next(protocol_config.m_cmd_args.begin()), protocol_config.m_cmd_args.end()) : protocol::cmd_args();
    m_stream_ptr    = stream::make_stream(m_executor, backend);
    {
      std::stringstream str;
      str << "starting child process [backend: "
          << (backend == stream::backend::tty ? "tty" : "pipe")
          << " | exe: " << exe
          << " | args: ";
      if (args.empty()) {
        str << "none";
      }
      else {
        for (auto it = args.begin(); it != args.end(); ++it) {
          str << *it;
          if (std::next(it) != args.end()) {
            str << " ";
          }
        }
      }
      str << " | environment: ";
      if (environment.empty()) {
        str << "none";
      }
      else {
        for (auto it = env_vars_copy.begin(); it != env_vars_copy.end(); ++it) {
          str << it->m_key << "=" << it->m_value;
          if (std::next(it) != env_vars_copy.end()) {
            str << ", ";
          }
        }
      }
      str << " | workdir: " << workdir << "]";
      m_logger->trace("{}", str.str());
    }
    {
      std::exception_ptr exception_ptr = nullptr;
      try {
        boost::filesystem::path exe_path(exe);
        if (exe_path.is_relative()) {
          boost::filesystem::path candidate = boost::filesystem::path(workdir) / exe_path;
          if (boost::filesystem::exists(candidate)) {
            exe_path = candidate;
          }
          else {
            exe_path = boost::process::search_path(exe);
          }
        }
        if (exe_path.empty()) {
#ifdef DEBUG_BUILD
          m_logger->warn("executable not found [exe: {}]", exe);
#endif
          throw std::system_error(error::code::server_error);
        }
        m_child = detail::process::make_child(m_stream_ptr,
                                              boost::process::exe(exe_path),
                                              boost::process::args(args),
                                              environment,
#ifndef _WIN32
                                              detail::process::uid::handler(user_info),
#endif
                                              boost::process::start_dir(boost::filesystem::path(workdir)),
                                              boost::process::on_exit = completion_handler,
                                              m_executor.context());
      }
      catch (...) {
        exception_ptr = std::current_exception();
      }
      if (exception_ptr) {
        try {
          std::rethrow_exception(exception_ptr);
        }
        catch (const std::system_error& system_error) {
          error_code = system_error.code();
        }
        catch (const boost::system::system_error& system_error) {
          error_code = system_error.code();
        }
        catch (const rstream::core::system_error& system_error) {
          error_code = system_error.code();
        }
        catch (...) {
          error_code = error::code::unknown_undefined_error;
        }
        if (error_code == error::code::unknown_undefined_error) {
          m_logger->warn("error has unexpected type [{}]", rstream::core::throwable::to_string(exception_ptr));
        }
      }
    }
  }
  if (error_code) {
#ifdef DEBUG_BUILD
    m_logger->warn("error starting child process [error_code: {}]", error_code.message());
#endif
    do_send_error(error_code);
  }
  else {
    set_state(state::connected);
    rstream::rtty::protobuf::Message message;
    message.mutable_ack();
    do_send_message(message, loop::null);
    run_loop();
  }
}

void server::impl::session::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  read_stdfd_loop();
  process_incoming_messages_loop();
  send_heartbeat();
}

void server::impl::session::read_stdfd_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_active_streams.insert(stream::type::std_out);
  if (m_stream_ptr->backend() == stream::backend::pipe) {
    m_active_streams.insert(stream::type::std_err);
  }
  for (const auto stream : m_active_streams) {
    do_read_stdfd(stream);
  }
}

void server::impl::session::do_read_stdfd(stream::type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto& buffer            = type == stream::type::std_out ? m_buffer_std_out : m_buffer_std_err;
  auto completion_handler = std::bind(&session::on_read_stdfd, shared_from_this(), std::placeholders::_1, std::placeholders::_2, type);
  m_stream_ptr->async_read_some(boost::asio::mutable_buffer(buffer.map().get_data(), buffer.get_size()), type, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_read_stdfd(const std::error_code& error_code, std::size_t size, stream::type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  bool eos = false;
  if (error_code) {
    if (error_code == boost::system::error_code(boost::asio::error::eof)) {
      eos = true;
    }
#ifdef _WIN32
    // on windows pipe streams return ERROR_BROKEN_PIPE rather than eof to indicate end of file
    else if (error_code == boost::system::error_code(boost::asio::error::broken_pipe)) {
      if (m_stream_ptr->backend() == stream::backend::pipe) {
        eos = true;
      }
    }
    else if (error_code.value() == ERROR_BROKEN_PIPE && error_code.category() == std::system_category()) {
      if (m_stream_ptr->backend() == stream::backend::tty) {
        eos = true;
      }
    }
#endif
#ifdef __linux__
    // on linux slave terminals return EIO rather than eos to indicate end of file
    else if (error_code == boost::system::error_condition(boost::system::errc::io_error)) {
      if (m_stream_ptr->backend() == stream::backend::tty) {
        eos = true;
      }
    }
#endif
    m_logger->trace("stream closed [stream: {}, eos: {}, error_code: {}]", type == stream::type::std_out ? "stdout" : "stderr", eos, error_code.message());
    m_active_streams.erase(type);
  }
  if (error_code && !eos) {
    on_error(error_code);
  }
  else {
    rstream::rtty::protobuf::Message message;
    auto data = message.mutable_data();
    data->set_type(type == stream::type::std_out ? rstream::rtty::protobuf::Data_Type_TYPE_STDOUT : rstream::rtty::protobuf::Data_Type_TYPE_STDERR);
    if (eos) {
      data->mutable_eos();
    }
    else {
      auto& buffer = type == stream::type::std_out ? m_buffer_std_out : m_buffer_std_err;
      data->set_data(buffer.map().get_const_data(), size);
    }
    do_send_message(message, eos ? loop::null : (type == stream::type::std_out ? loop::read_std_out : loop::read_std_err));
  }
}

void server::impl::session::do_send_error(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting && m_state != state::connected) {
    return;
  }
  set_state(state::disconnecting);
  rstream::rtty::protobuf::Message message;
  error::code code;
  if (error_code.category() == std::error_code((error::code){}).category()) {
    code = (error::code)error_code.value();
  }
  else {
    code = error::code::unknown_undefined_error;
  }
  message.mutable_error()->set_msg(error_code.message());
  if (error_code && !m_error_code) {
    m_error_code = error_code;
  }
  do_send_message(message, loop::exit);
}

void server::impl::session::do_send_close(int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (m_active_streams.empty()) {
    set_state(state::disconnecting);
    rstream::rtty::protobuf::Message message;
    message.mutable_close()->set_return_code(code);
    do_send_message(message, loop::exit);
  }
  else {
    m_return_code = code;
#ifdef _WIN32
    if (m_stream_ptr->backend() == stream::backend::tty) {
      std::dynamic_pointer_cast<stream::pty_windows>(m_stream_ptr)->cancel();
    }
#endif
  }
}

void server::impl::session::do_send_message(const rstream::rtty::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("sending message to peer\n{}", core::helpers::to_json_string(message));
#endif
  std::size_t buffer_size = message.ByteSizeLong();
  auto buffer             = rstream::core::make_buffer_allocated(buffer_size);
  message.SerializeToArray(buffer.map().get_data(), buffer_size);
  auto completion_handler = std::bind(&session::on_send_message, shared_from_this(), std::placeholders::_1, loop);
  m_queue->async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_send_message(const std::error_code& error_code, enum loop loop)
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
      case loop::read_std_out:
        do_read_stdfd(stream::type::std_out);
        break;
      case loop::read_std_err:
        do_read_stdfd(stream::type::std_err);
        break;
      case loop::heartbeat:
        do_send_heartbeat();
        break;
      case loop::exit:
        if (m_websocket) {
          do_close_websocket();
        }
        else {
          on_close(error_code);
        }
        break;
      case loop::null: {
        if (m_state == state::connected && m_active_streams.empty() && m_return_code) {
          do_send_close(m_return_code.get());
        }
      } break;
      default:
        break;
    }
  }
}

void server::impl::session::process_incoming_messages_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_incoming_message();
}

void server::impl::session::do_read_incoming_message()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  m_buffer_socket.reset_size();
  auto completion_handler = std::bind(&session::on_read_incoming_data, shared_from_this(), std::placeholders::_1);
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

void server::impl::session::on_read_incoming_data(const std::error_code& error_code)
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

void server::impl::session::on_read_incoming_message(const rstream::rtty::protobuf::Message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  bool do_read_messages = false;
  std::error_code error_code;
#ifdef DEBUG_BUILD
  m_logger->trace("received message from peer\n{}", core::helpers::to_json_string(message));
#endif
  if (!is_message_expected(m_state, message)) {
    error_code = error::code::unexpected_message;
  }
  else {
    using payload_type = rstream::rtty::protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kOpen) {
      protocol::config protocol_config;
      detail::convert(protocol_config, message.open().config());
      on_open(protocol_config);
    }
    else if (message_type == payload_type::kError) {
      cancel_internal(error::code::client_error);
    }
    else if (message_type == payload_type::kData) {
      do_process_data(message.data());
    }
    else if (message_type == payload_type::kParameter) {
      const auto parameter = message.parameter();
      if (parameter.has_terminal_size()) {
        do_read_messages = true;
        terminal_size terminal_size;
        detail::convert(terminal_size, parameter.terminal_size());
        set_terminal_size(terminal_size, error_code);
      }
      else {
        error_code = error::code::unexpected_message;
      }
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

void server::impl::session::do_process_data(const rstream::rtty::protobuf::Data& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (data.type() != rstream::rtty::protobuf::Data::TYPE_STDIN) {
    on_error(error::code::unexpected_message);
  }
  else {
    auto buffer = data.has_eos() ? nullptr : std::make_shared<std::string>(data.data());
    do_process_data(buffer);
  }
}

void server::impl::session::do_process_data(const std::shared_ptr<std::string> buffer)
{
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (buffer == nullptr) {
    m_logger->debug("closing stdin after receiving EOS from peer");
    if (m_stream_ptr->backend() == stream::backend::pipe) {
      std::dynamic_pointer_cast<stream::pipe>(m_stream_ptr)->close(stream::type::std_in);
    }
    on_process_data(std::error_code());
  }
  else {
    auto handler = [ptr = shared_from_this(), buffer](const std::error_code& error_code, std::size_t) {
      ptr->on_process_data(error_code);
    };
    auto completion_handler = boost::asio::bind_executor(m_strand, handler);
    auto boost_buffer       = boost::asio::const_buffer(buffer->data(), buffer->size());
    m_stream_ptr->async_write(boost_buffer, stream::type::std_in, completion_handler);
  }
}

void server::impl::session::on_process_data(const std::error_code& error_code)
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

void server::impl::session::on_child_exit(const std::error_code& error_code, int code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  m_logger->info("child exited [exit_code: {}, error_code: {}]", code, error_code ? error_code.message() : "none");
  if (error_code) {
    do_send_error(error_code);
  }
  else {
    do_send_close(code);
  }
}

void server::impl::session::do_close_websocket()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&session::on_close_websocket, shared_from_this(), std::placeholders::_1);
  m_websocket->async_close(boost::beast::websocket::close_code::normal, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_close_websocket(const std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  (void)error_code;
  on_close(std::error_code());
}

void server::impl::session::set_terminal_size(const terminal_size& terminal_size, std::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_stream_ptr->backend() == stream::backend::tty) {
    std::dynamic_pointer_cast<stream::pty>(m_stream_ptr)->set_window_size(terminal_size, error_code);
  }
}

void server::impl::session::send_heartbeat()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_send_heartbeat();
}

void server::impl::session::do_send_heartbeat()
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

bool server::impl::session::is_message_expected(state state, const rstream::rtty::protobuf::Message& message)
{
  using payload_type                                                                               = rstream::rtty::protobuf::Message::PayloadCase;
  static const std::map<server::impl::session::state, std::set<payload_type>> compatibility_matrix = {
      {state::connecting, {payload_type::kOpen}},
      {state::connected, {payload_type::kData, payload_type::kParameter, payload_type::kError, payload_type::kHeartbeat}},
      {state::disconnecting, {payload_type::kData, payload_type::kParameter, payload_type::kError, payload_type::kHeartbeat}},
  };
  const auto& set = compatibility_matrix.find(state)->second;
  return set.find(message.payload_case()) != set.end();
}

void parse_environment(boost::process::environment& dst, const protocol::env_vars& src)
{
  dst.clear();
  for (const auto& env_var : src) {
    dst[env_var.m_key] = env_var.m_value;
  }
}

}  // namespace rtty
}  // namespace rstream
