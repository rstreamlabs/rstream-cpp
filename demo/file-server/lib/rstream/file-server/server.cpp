// See LICENSE file in the project root for license information.

#include "server.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/socket_base.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/object_id.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#endif
#include <rstream/io/error.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/stream.hpp>
#endif

#include "api.hpp"
#include "error.hpp"

#include "file-server.hpp"

namespace rstream {
namespace file_server {

template <class body, class allocator, class send_func>
using endpoints_type = std::map<boost::beast::http::verb, std::map<std::string, void (*)(const context&, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&, send_func&)>>;

template <class body, class allocator, class send_func>
static const endpoints_type<body, allocator, send_func> endpoints = {
    {boost::beast::http::verb::get, {{"^\\/?$", api_redirect_www}, {"^\\/www(\\/.*)?$", api_www}, {"^\\/api\\/file\\/.*$", api_get_file}}},
};

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

  using socket_type = protocol_type::socket;

  session(socket_type&& socket, const session_id_type& session_id, const context& ctx);

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  enum class state {
    null         = 0,
    connected    = 1,
    disconnected = 2
  };

  class send;

  void set_state(state state);

  void async_run_internal(async_run_completion_handler&& handler);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  void do_read_request();

  void on_read_request(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_process_request();

  template <class body, class allocator, class send_func>
  void handle_request(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>& request, send_func func);

  void on_write(bool close, const boost::system::error_code& error_code, std::size_t bytes_transferred);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  socket_type m_socket;

  const session_id_type m_session_id;

  const context m_ctx;

  rstream::core::logger m_logger;

  state m_state;

  boost::beast::flat_buffer m_buffer;

  boost::beast::http::request<boost::beast::http::string_body> m_request;

  std::shared_ptr<void> m_response;

  async_run_completion_handler m_handler;
};

class RSTREAM_GNUC_INTERNAL server::impl::session::send {
 public:
  send(server::impl::session::ptr session);

  template <bool is_request, class body, class fields>
  void operator()(boost::beast::http::message<is_request, body, fields>&& msg) const;

 private:
  server::impl::session::ptr m_session;
};

server::server(const executor_type& executor, const config& config, const settings_server& settings)
    : io::io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

server::~server() noexcept
{
  try {
    m_impl->cancel();
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
      m_logger({"rstream", "file-server", "server", fmt::format("#{}", fmt::ptr(this))}),
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
      auto session_id        = generate_session_id();
      struct context context = {.m_workdir = boost::filesystem::system_complete(m_config.m_workdir)};
      auto session_ptr       = std::make_shared<session>(std::move(m_socket), session_id, context);
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

server::impl::session::session(socket_type&& socket, const session_id_type& session_id, const context& ctx)
    : m_executor(socket.get_executor()),
      m_strand(socket.get_executor()),
      m_socket(std::move(socket)),
      m_session_id(session_id),
      m_ctx(ctx),
      m_logger({"rstream", "file-server", "session", fmt::format("#{}", session_id)}),
      m_state(state::null)
{
  std::stringstream str;
  boost::system::error_code error;
  str << m_socket.remote_endpoint(error);
  if (!error) {
    m_logger->debug("new connection from [{}]", str.str());
  }
}

void server::impl::session::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::session::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&server::impl::session::cancel_internal, shared_from_this(), boost::system::error_code()));
}

void server::impl::session::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
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
  set_state(state::connected);
  do_read_request();
}

void server::impl::session::cancel_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = error_code ? error_code : error::code::operation_aborted;
  on_error(cause);
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

void server::impl::session::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  set_state(state::disconnected);
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), error_code);
  }
  m_handler = nullptr;
}

void server::impl::session::do_read_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_request               = {};
  m_response              = nullptr;
  auto completion_handler = std::bind(&server::impl::session::on_read_request, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_read(m_socket, m_buffer, m_request, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session::on_read_request(const boost::system::error_code& error_code, std::size_t bytes_transferred)
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
    do_process_request();
  }
}

void server::impl::session::do_process_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_request.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("HTTP request :\n{}", str.str());
  }
#else
  m_logger->trace("HTTP request '{}' '{}'", std::string(m_request.method_string()), std::string(m_request.target()));
#endif
  boost::system::error_code error_code;
  try {
    handle_request(m_ctx, m_request, server::impl::session::send(shared_from_this()));
  }
  catch (const boost::system::system_error& system_error) {
    error_code = system_error.code();
  }
  catch (const rstream::core::system_error& system_error) {
    error_code = system_error.code();
  }
  catch (...) {
    error_code = rstream::io::error::code::unknown_undefined_error;
  }
  if (error_code) {
    on_error(error_code);
  }
}

template <class body, class allocator, class send_func>
void server::impl::session::handle_request(const context& ctx, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>& request, send_func func)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  const auto& target                                                                                                                        = request.target();
  std::function<void(const context&, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&, send_func&)> func_ptr = nullptr;
  {
    auto method = endpoints<body, allocator, send_func>.find(request.method());
    if (method != endpoints<body, allocator, send_func>.end()) {
      typename std::map<std::string, void (*)(const context&, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&, send_func&)>::const_iterator endpoint;
      for (endpoint = method->second.begin(); endpoint != method->second.end(); ++endpoint) {
        if (std::regex_search(std::string(target), std::regex(endpoint->first, std::regex_constants::ECMAScript))) {
          break;
        }
      }
      if (endpoint != method->second.end()) {
        func_ptr = endpoint->second;
      }
    }
  }
  if (func_ptr == nullptr) {
    func_ptr = std::bind((void (*)(const context&, boost::beast::http::request<body, boost::beast::http::basic_fields<allocator>>&, send_func&, const std::string&))bad_request<body, allocator, send_func>, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, "Invalid Endpoint");
  }
  std::exception_ptr error = nullptr;
  try {
    func_ptr(ctx, request, func);
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    m_logger->trace("an error occurred while processing request: {}", rstream::core::throwable(error).what());
    enum class error_type {
      not_found,
      forbidden,
      internal_error
    };
    auto get_error_type = [](std::exception_ptr error) -> error_type {
      try {
        std::rethrow_exception(error);
      }
      catch (const boost::system::system_error& error) {
        if (error.code() == boost::system::errc::no_such_file_or_directory) {
          return error_type::not_found;
        }
        else if (error.code() == boost::system::errc::permission_denied
                 || error.code() == boost::system::errc::operation_not_permitted) {
          return error_type::forbidden;
        }
      }
      catch (...) {
      }
      return error_type::internal_error;
    };
    try {
      auto error_type = get_error_type(error);
      if (error_type == error_type::not_found) {
        not_found(ctx, request, func);
      }
      else if (error_type == error_type::forbidden) {
        forbidden(ctx, request, func);
      }
      else {
        internal_server_error(ctx, request, func);
      }
    }
    catch (...) {
    }
  }
}

void server::impl::session::on_write(bool close, const boost::system::error_code& error_code, std::size_t bytes_transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)bytes_transferred;
  if (error_code) {
    on_error(error_code);
  }
  else if (close) {
    // this means we should close the connection, usually because
    // the response indicated the "Connection: close" semantic
    on_close(boost::system::error_code());
  }
  else {
    do_read_request();
  }
}

server::impl::session::send::send(server::impl::session::ptr session)
    : m_session(session)
{
}

template <bool is_request, class body, class fields>
void server::impl::session::send::operator()(boost::beast::http::message<is_request, body, fields>&& msg) const
{
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << msg.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_session->m_logger->trace("HTTP response :\n{}", str.str());
  }
#else
  m_session->m_logger->trace("HTTP response {}", msg.result_int());
#endif
  auto sp               = std::make_shared<boost::beast::http::message<is_request, body, fields>>(std::move(msg));
  m_session->m_response = sp;
  boost::beast::http::async_write(m_session->m_socket, *sp, boost::asio::bind_executor(m_session->m_strand, boost::beast::bind_front_handler(&server::impl::session::on_write, m_session, sp->need_eof())));
}

}  // namespace file_server
}  // namespace rstream
