// See LICENSE file in the project root for license information.

#include "server.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/buffers_adaptor.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/protobuf.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/object_id.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io/error.hpp>
#include <rstream/io/payloader.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/websocket.hpp>
#include <rstream/io/stream.hpp>
#endif

#include <rstream/nperf/protobuf/messages.pb.h>

#include "detail/convert.hpp"
#include "error.hpp"
#include "nperf.hpp"

namespace rstream {
namespace nperf {

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

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void on_started();

  void do_accept();

  void on_accept(const boost::system::error_code& error_code);

  void cancel_internal();

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  void on_session_closed(const boost::system::error_code& error_code, const session_id_type& session_id);

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

  boost::system::error_code m_error_code;

  state_server_changed_signal_type m_state_changed_signal;
};

class RSTREAM_GNUC_INTERNAL server::impl::session : public std::enable_shared_from_this<session> {
 public:
  using ptr = std::shared_ptr<session>;

  session(socket_type&& socket, const settings_server& settings, const session_id_type& session_id);

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  using websocket_type = std::shared_ptr<boost::beast::websocket::stream<socket_type&, false>>;

  using payloader_type = std::shared_ptr<rstream::io::payloader<socket_type&>>;

  enum class loop {
    null,
    read_data,
    read_dummy,
    write_data,
    recv_ping
  };

  enum class state {
    null          = 0,
    connecting    = 1,
    connected     = 2,
    disconnecting = 3,
    disconnected  = 4
  };

  using state_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  using on_activity_signal_type = boost::signals2::signal_type<void(boost::beast::websocket::frame_type, const boost::beast::string_view&), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code = boost::system::error_code());

  void async_run_internal(async_run_completion_handler&& handler);

  void do_read_http_request();

  void on_read_http_request(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_process_http_request();

  void do_write_http_response();

  void on_write_http_response(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_accept_websocket();

  void do_read_incoming_protobuf_message(enum loop loop);

  void on_read_incoming_protobuf_data(const boost::system::error_code& error_code, enum loop loop);

  void on_read_incoming_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop);

  void do_send_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop);

  void on_send_protobuf_message(const boost::system::error_code& error_code, enum loop loop);

  void on_open(unsigned int options);

  void on_ping(const std::string& data);

  void on_accept(const boost::system::error_code& error_code);

  void run_loop();

  void ping_loop();

  void download_loop();

  void upload_loop();

  void do_read_write(enum loop loop);

  void on_read_write(const boost::system::error_code& error_code, std::size_t, enum loop loop);

  void on_control_callback(boost::beast::websocket::frame_type kind, const boost::beast::string_view& payload);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  socket_type m_socket;

  websocket_type m_websocket;

  payloader_type m_payloader;

  const settings_server m_settings;

  rstream::core::logger m_logger;

  state m_state;

  options m_options;

  boost::beast::http::request<boost::beast::http::string_body> m_http_request;

  boost::beast::http::response<boost::beast::http::empty_body> m_http_response;

  async_run_completion_handler m_handler;

  boost::system::error_code m_error_code;

  state_changed_signal_type m_state_changed_signal;

  rstream::core::memory m_buffer;

  boost::beast::buffers_adaptor<boost::asio::mutable_buffer> m_http_buffers_adaptor;
};

server::server(const executor_type& executor, const config& config, const settings_server& settings)
    : io::io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

server::~server() noexcept
{
  try {
    cancel();
  }
  catch (...) {
    return;
  }
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
      m_logger({"rstream", "nperf", "server", fmt::format("#{}", fmt::ptr(this))}),
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
    boost::asio::steady_timer m_timer;
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
  auto on_timer_cb         = [task_ptr, ptr](const boost::system::error_code& error_code) {
    if (task_ptr->m_complete) {
      return;
    }
    if (!error_code) {
      ptr->on_error(error::code::operation_timeout);
    }
  };
  task_ptr->m_timer.expires_after(std::chrono::milliseconds(timeout_ms));
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

void server::impl::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
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
      cause = error::code::no_valid_endpoint;
    }
    else {
      auto endpoint = results.begin()->endpoint();
#ifdef RSTREAM_WITH_IO_STREAMS
      m_acceptor.open(endpoint, cause);
#else
      m_acceptor.open(endpoint.protocol(), cause);
#endif
#ifndef RSTREAM_WITH_IO_STREAMS
      if (!cause) {
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true), cause);
      }
#endif
      if (!cause) {
        m_acceptor.bind(endpoint, cause);
      }
      if (!cause) {
        m_acceptor.listen(boost::asio::socket_base::max_listen_connections, cause);
      }
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

void server::impl::on_accept(const boost::system::error_code& error_code)
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
      auto session_ptr = std::make_shared<session>(std::move(m_socket), m_settings, session_id);
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
    do_close(boost::system::error_code());
  }
  else if (m_state == state::starting) {
    on_close(boost::system::error_code());
  }
}

void server::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::do_close(const boost::system::error_code& error_code)
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

void server::impl::on_close(const boost::system::error_code& error_code)
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

void server::impl::on_session_closed(const boost::system::error_code& error_code, const session_id_type& session_id)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)error_code;
  m_logger->trace("session closed [session_id: {}, error_code: {}]", session_id, (error_code ? error_code.message() : "none"));
  m_sessions.erase(session_id);
  if (m_state == state::stopping) {
    if (m_sessions.empty()) {
      on_close(boost::system::error_code());
    }
  }
}

server::impl::session_id_type server::impl::generate_session_id()
{
  return rstream::core::object_id();
}

server::impl::session::session(socket_type&& socket, const settings_server& settings, const session_id_type& session_id)
    : m_executor(socket.get_executor()),
      m_strand(socket.get_executor()),
      m_socket(std::move(socket)),
      m_settings(settings),
      m_logger({"rstream", "nperf", "session", fmt::format("#{}", session_id)}),
      m_state(state::null),
      m_options(0),
      m_http_buffers_adaptor(boost::asio::mutable_buffer(nullptr, 0))
{
#ifndef RSTREAM_WITH_IO_STREAMS
  // boost::asio::ip::tcp::no_delay no_delay(true);
  // m_socket.set_option(no_delay);
  // boost::asio::socket_base::keep_alive keep_alive(true);
  // m_socket.set_option(keep_alive);
  // boost::asio::socket_base::receive_buffer_size receive_buffer_size(512*1024*1024);
  // m_socket.set_option(receive_buffer_size);
  // boost::asio::socket_base::send_buffer_size send_buffer_size(512*1024*1024);
  // m_socket.set_option(send_buffer_size);
  // m_socket.set_option(boost::asio::detail::socket_option::integer<SOL_SOCKET, TCP_MAXSEG>(65536));
#endif
  {
    std::stringstream str;
    boost::system::error_code error;
    str << m_socket.remote_endpoint(error);
    if (!error) {
      m_logger->debug("new connection from [{}]", str.str());
    }
  }
  if (m_settings.m_common.m_protocol == protocol::websocket) {
    m_websocket = std::make_shared<websocket_type::element_type>(m_socket);
  }
  else if (m_settings.m_common.m_protocol == protocol::plain) {
    m_payloader = std::make_shared<payloader_type::element_type>(m_socket);
  }
}

void server::impl::session::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::session::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&server::impl::session::cancel_internal, shared_from_this(), error::code::operation_aborted));
}

void server::impl::session::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
  m_state_changed_signal(state);
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
  m_logger->debug("session state changed to '{}'", str.str());
}

void server::impl::session::arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code)
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
    boost::asio::steady_timer m_timer;
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
  auto on_timer_cb         = [task_ptr, ptr, cause = error_code](const boost::system::error_code& error_code) {
    if (task_ptr->m_complete || error_code) {
      return;
    }
    ptr->cancel_internal(cause);
  };
  task_ptr->m_timer.expires_after(std::chrono::milliseconds(timeout_ms));
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
  m_buffer = rstream::core::make_memory_allocated(m_settings.m_common.m_buffer_size);
  set_state(state::connecting);
  arm_state_timer(m_settings.m_common.m_timeouts_open_close_ms, error::code::operation_timeout);
  if (m_websocket) {
    do_read_http_request();
  }
  else {
    do_read_incoming_protobuf_message(loop::null);
  }
}

void server::impl::session::do_read_http_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer.reset_size();
  m_http_request          = {};
  m_http_buffers_adaptor  = boost::beast::buffers_adaptor<boost::asio::mutable_buffer>(boost::asio::mutable_buffer(m_buffer.get_data(), m_buffer.get_size()));
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
  // see if it is a websocket upgrade
  if (boost::beast::websocket::is_upgrade(m_http_request)) {
    if (m_http_request.method() == boost::beast::http::verb::get) {
      auto target = m_http_request.target();
      if (target == "/ping") {
        m_options |= option::ping;
      }
      else if (target == "/download") {
        m_options |= option::download;
      }
      else if (target == "/upload") {
        m_options |= option::upload;
      }
    }
  }
  if (m_options != 0) {
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
  m_http_response         = boost::beast::http::response<boost::beast::http::empty_body>(boost::beast::http::status::bad_request, m_http_request.version());
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
  m_websocket->read_message_max(m_buffer.get_size());
  if (m_options & option::download || m_options & option::upload) {
    m_websocket->write_buffer_bytes(m_buffer.get_size() + 14);
  }
  m_websocket->auto_fragment(false);
  // set the control callback. This will be called
  // on every incoming ping, pong, and close frame
  {
    auto completion_handler = std::bind(&session::on_control_callback, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
    m_websocket->control_callback(boost::asio::bind_executor(m_strand, completion_handler));
  }
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
    auto completion_handler = std::bind(&session::on_accept, shared_from_this(), std::placeholders::_1);
    m_websocket->async_accept(m_http_request, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
  }
}

void server::impl::session::do_read_incoming_protobuf_message(enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  m_buffer.reset_size();
  auto completion_handler = std::bind(&session::on_read_incoming_protobuf_data, shared_from_this(), std::placeholders::_1, loop);
  m_payloader->async_recv(m_buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_read_incoming_protobuf_data(const boost::system::error_code& error_code, enum loop loop)
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
    rstream::nperf::protobuf::Message message;
    if (core::detail::parse_protobuf_message(message, m_buffer.get_const_data(), m_buffer.get_size())) {
      on_read_incoming_protobuf_message(message, loop);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

void server::impl::session::on_read_incoming_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  boost::system::error_code error_code;
#ifdef DEBUG_BUILD
  m_logger->trace("received message from peer\n{}", core::helpers::to_json_string(message));
#endif
  auto is_message_expected = [this](const rstream::nperf::protobuf::Message& message) {
    using payload_type = rstream::nperf::protobuf::Message::PayloadCase;
    std::set<payload_type> expected_messages;
    if (m_state == state::connecting) {
      expected_messages.insert(payload_type::kOpen);
    }
    else if (m_state > state::connecting && m_options & option::ping) {
      expected_messages.insert(payload_type::kPing);
    }
    return expected_messages.find(message.payload_case()) != expected_messages.end();
  };
  if (!is_message_expected(message)) {
    error_code = error::code::protocol_error;
  }
  else {
    using payload_type = rstream::nperf::protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kOpen) {
      if (message.open().has_config()) {
        unsigned int options = 0;
        detail::convert(options, message.open().config().option());
        on_open(options);
      }
      else {
        error_code = error::code::protocol_error;
      }
    }
    else if (message_type == payload_type::kPing) {
      on_ping(message.ping().data());
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_write(loop);
  }
}

void server::impl::session::do_send_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop)
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
  rstream::core::buffer buffer;
  if (!rstream::core::detail::serialize_protobuf_message(message, buffer)) {
    on_error(error::code::protocol_error);
    return;
  }
  auto completion_handler = std::bind(&session::on_send_protobuf_message, shared_from_this(), std::placeholders::_1, loop);
  m_payloader->async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_send_protobuf_message(const boost::system::error_code& error_code, enum loop loop)
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
  else if (m_state == state::connecting) {
    on_accept(boost::system::error_code());
  }
  else {
    do_read_write(loop);
  }
}

void server::impl::session::on_open(unsigned int options)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  m_options = options;
  rstream::nperf::protobuf::Message message;
  message.mutable_ack();
  do_send_protobuf_message(message, loop::null);
}

void server::impl::session::on_ping(const std::string& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (!(m_options & option::ping)) {
    return;
  }
  rstream::nperf::protobuf::Message message;
  message.mutable_pong()->set_data(data);
  do_send_protobuf_message(message, loop::recv_ping);
}

void server::impl::session::on_accept(const boost::system::error_code& error_code)
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
    set_state(state::connected);
    run_loop();
  }
}

void server::impl::session::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer.reset_size();
  arm_state_timer(m_settings.m_common.m_timeouts_max_time_ms, error::code::operation_timeout);
  if (m_options & option::ping) {
    ping_loop();
  }
  else if (m_options & option::download) {
    download_loop();
  }
  else {
    upload_loop();
  }
}

void server::impl::session::ping_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_websocket) {
    do_read_write(loop::read_dummy);
  }
  else {
    do_read_write(loop::recv_ping);
  }
}

void server::impl::session::download_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  rstream::core::random_bytes(m_buffer.get_data(), m_buffer.get_size());
  do_read_write(loop::write_data);
  do_read_write(loop::read_dummy);
}

void server::impl::session::upload_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_write(loop::read_data);
}

void server::impl::session::do_read_write(enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (loop == loop::null) {
    return;
  }
  auto completion_handler = std::bind(&session::on_read_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2, loop);
  if (loop == loop::read_data) {
    if (m_websocket) {
      m_http_buffers_adaptor = boost::beast::buffers_adaptor<boost::asio::mutable_buffer>(boost::asio::mutable_buffer(m_buffer.get_data(), m_buffer.get_size()));
      m_websocket->async_read(m_http_buffers_adaptor, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
    }
    else {
      m_socket.async_read_some(boost::asio::mutable_buffer(m_buffer.get_data(), m_buffer.get_size()), boost::asio::bind_executor(m_strand, completion_handler));
    }
  }
  else if (loop == loop::read_dummy) {
    auto buffer  = std::make_shared<boost::beast::flat_static_buffer<1>>();
    auto handler = [completion_handler, loop, buffer](const boost::system::error_code& error_code, std::size_t bytes_transferred) {
      completion_handler(error_code, bytes_transferred, loop);
    };
    if (m_websocket) {
      m_websocket->async_read(*buffer, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, handler));
    }
    else {
      m_socket.async_read_some(buffer->prepare(1), boost::asio::bind_executor(m_strand, handler));
    }
  }
  else if (loop == loop::write_data) {
    auto buffer = boost::asio::const_buffer(m_buffer.get_data(), m_buffer.get_size());
    if (m_websocket) {
      m_websocket->async_write(buffer, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
    }
    else {
      m_socket.async_write_some(buffer, boost::asio::bind_executor(m_strand, completion_handler));
    }
  }
  else if (loop == loop::recv_ping) {
    if (m_payloader) {
      do_read_incoming_protobuf_message(loop::null);
    }
  }
}

void server::impl::session::on_read_write(const boost::system::error_code& error_code, std::size_t, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (!error_code) {
    do_read_write(loop);
  }
  else {
    on_close(error_code == boost::beast::websocket::error::closed ? boost::system::error_code() : error_code);
  }
}

void server::impl::session::on_control_callback(boost::beast::websocket::frame_type kind, const boost::beast::string_view& payload)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  (void)kind;
  (void)payload;
}

void server::impl::session::cancel_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (m_state == state::connected) {
    do_close(error_code);
  }
  else if (m_state == state::connecting || m_state == state::disconnecting) {
    on_close(error_code);
  }
}

void server::impl::session::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::session::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  set_state(state::disconnecting);
  if (error_code && !m_error_code) {
    m_error_code = error_code;
  }
  arm_state_timer(m_settings.m_common.m_timeouts_open_close_ms);
  auto completion_handler = std::bind(&session::on_close, shared_from_this(), std::placeholders::_1);
  if (m_websocket) {
    auto cause = m_error_code ? boost::beast::websocket::close_code::try_again_later : boost::beast::websocket::close_code::normal;
    m_websocket->async_close(cause, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
  }
  else {
    completion_handler(boost::system::error_code());
  }
}

void server::impl::session::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = m_error_code ? m_error_code : error_code;
  set_state(state::disconnected);
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler = nullptr;
}

}  // namespace nperf
}  // namespace rstream
