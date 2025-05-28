// See LICENSE file in the project root for license information.

#include "server.hpp"

#include <map>
#include <set>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/object_id.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>
#endif

#include "error.hpp"

namespace rstream {
namespace ncat {

class RSTREAM_GNUC_INTERNAL server::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_server& settings);

  virtual ~impl() = default;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class session;

  class session_proxy;

  class session_exec;

#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = io::stream;
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

  core::logger m_logger;

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

class RSTREAM_GNUC_INTERNAL server::impl::session {
 public:
  virtual void async_run(async_run_completion_handler&& handler) = 0;

  virtual void cancel() = 0;
};

class RSTREAM_GNUC_INTERNAL server::impl::session_proxy : public session, public std::enable_shared_from_this<session_proxy> {
 public:
  session_proxy(socket_type&& downstream_socket, const settings_server& settings, const session_id_type& session_id, const io::address& upstream_address);

  void async_run(async_run_completion_handler&& handler) override;

  void cancel() override;

 private:
  enum class state {
    null         = 0,
    connecting   = 1,
    connected    = 2,
    disconnected = 3
  };

  enum class type {
    downstream,
    upstream
  };

  using state_session_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code);

  void async_run_internal(async_run_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_open(const resolver_type::results_type& endpoints);

  void on_open(const boost::system::error_code& error_code);

  void on_connected();

  void run_loop();

  void do_read(type type);

  void on_read(const boost::system::error_code& error_code, std::size_t size, type type);

  void do_write(type type);

  void on_write(const boost::system::error_code& error_code, std::size_t size, type type);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const settings_server m_settings;

  socket_type m_downstream_socket;

  socket_type m_upstream_socket;

  resolver_type m_resolver;

  const session_id_type m_session_id;

  const io::address m_upstream_address;

  core::logger m_logger;

  state m_state;

  async_run_completion_handler m_handler;

  std::shared_ptr<core::buffer> m_buffer_read_downstream;

  std::shared_ptr<core::buffer> m_buffer_read_upstream;

  boost::system::error_code m_error_code;

  state_session_changed_signal_type m_state_changed_signal;
};

class RSTREAM_GNUC_INTERNAL server::impl::session_exec : public session, public std::enable_shared_from_this<session_exec> {
 public:
  session_exec(socket_type&& downstream_socket, const settings_server& settings, const session_id_type& session_id, const exec& exec);

  void async_run(async_run_completion_handler&& handler) override;

  void cancel() override;

 private:
  const settings_server m_settings;
};

server::server(const executor_type& executor, const config& config, const settings_server& settings)
    : io::io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

server::~server()
{
  m_impl->cancel();
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
      m_logger({"rstream", "ncat", "server", fmt::format("#{}", fmt::ptr(this))}),
      m_acceptor(executor),
      m_socket(executor),
      m_state(state::null),
      m_resolver(executor)
{
  if (m_config.m_remote.type() != typeid(io::address)) {
    throw std::runtime_error("this feature is not implemented yet");
  }
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
  auto on_timer_cb         = [task_ptr, ptr](const boost::system::error_code& error_code) {
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
  arm_state_timer(m_settings.m_timeouts_ms.m_start);
  do_resolve_host();
}

void server::impl::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_config.m_local.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_config.m_local.host(), m_config.m_local.port(), boost::asio::bind_executor(m_strand, completion_handler));
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
      auto session_id                      = generate_session_id();
      std::shared_ptr<session> session_ptr = nullptr;
      if (m_config.m_remote.type() == typeid(io::address)) {
        session_ptr = std::make_shared<session_proxy>(std::move(m_socket), m_settings, session_id, boost::get<io::address>(m_config.m_remote));
      }
      else if (m_config.m_remote.type() == typeid(exec)) {
        session_ptr = std::make_shared<session_exec>(std::move(m_socket), m_settings, session_id, boost::get<exec>(m_config.m_remote));
      }
      if (session_ptr) {
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
      else {
        m_logger->trace("session creation failed, closing connection");
        boost::system::error_code tmp;
        m_socket.close(tmp);
      }
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

server::impl::session_proxy::session_proxy(socket_type&& downstream_socket, const settings_server& settings, const session_id_type& session_id, const io::address& upstream_address)
    : m_executor(downstream_socket.get_executor()),
      m_strand(downstream_socket.get_executor()),
      m_settings(settings),
      m_downstream_socket(std::move(downstream_socket)),
      m_upstream_socket(downstream_socket.get_executor()),
      m_resolver(downstream_socket.get_executor()),
      m_session_id(session_id),
      m_upstream_address(upstream_address),
      m_logger({"rstream", "ncat", "session", fmt::format("#{}", session_id)}),
      m_state(state::null)
{
  std::stringstream str;
  {
    boost::system::error_code error;
    str << m_downstream_socket.remote_endpoint(error);
    if (!error) {
      m_logger->debug("new connection from [{}]", str.str());
    }
  }
}

void server::impl::session_proxy::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session_proxy::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::session_proxy::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&session_proxy::cancel_internal, shared_from_this(), error::code::operation_aborted));
}

void server::impl::session_proxy::set_state(state state)
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
    case state::disconnected:
      str << "disconnected";
      break;
    default:
      break;
  }
  m_logger->debug("session state changed to '{}'", str.str());
}

void server::impl::session_proxy::arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code)
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
  auto on_timer_cb         = [task_ptr, ptr, cause = error_code](const boost::system::error_code& error_code) {
    if (task_ptr->m_complete || error_code) {
      return;
    }
    ptr->cancel_internal(cause);
  };
  task_ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
  auto completion_handler = boost::asio::bind_executor(ptr->m_strand, on_timer_cb);
  task_ptr->m_timer.async_wait(completion_handler);
}

void server::impl::session_proxy::async_run_internal(async_run_completion_handler&& handler)
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
  m_buffer_read_downstream = std::make_shared<core::buffer>(core::make_memory_allocated(m_settings.m_read_downstream_buffer_size_bytes));
  m_buffer_read_upstream   = std::make_shared<core::buffer>(core::make_memory_allocated(m_settings.m_read_upstream_buffer_size_bytes));
  set_state(state::connecting);
  arm_state_timer(m_settings.m_timeouts_ms.m_open, error::code::operation_timeout);
  do_resolve_host();
}

void server::impl::session_proxy::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&server::impl::session_proxy::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_upstream_address.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_upstream_address.host(), m_upstream_address.port(), boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void server::impl::session_proxy::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
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
    do_open(results);
  }
}

void server::impl::session_proxy::do_open(const resolver_type::results_type& endpoints)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&server::impl::session_proxy::on_open, shared_from_this(), std::placeholders::_1);
  boost::asio::async_connect(m_upstream_socket, endpoints, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session_proxy::on_open(const boost::system::error_code& error_code)
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

void server::impl::session_proxy::on_connected()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  set_state(state::connected);
  std::stringstream str;
  {
    boost::system::error_code error;
    str << m_upstream_socket.remote_endpoint(error);
    if (!error) {
      m_logger->debug("forwarding connection to [{}]", str.str());
    }
  }
  run_loop();
}

void server::impl::session_proxy::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read(type::downstream);
  do_read(type::upstream);
}

void server::impl::session_proxy::do_read(type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto& buffer = type == type::downstream ? *m_buffer_read_downstream : *m_buffer_read_upstream;
  buffer.reset_size();
  auto completion_handler = std::bind(&session_proxy::on_read, shared_from_this(), std::placeholders::_1, std::placeholders::_2, type);
  auto& socket            = type == type::downstream ? m_downstream_socket : m_upstream_socket;
  socket.async_read_some(core::helpers::mutable_memory_sequence(buffer), boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session_proxy::on_read(const boost::system::error_code& error_code, std::size_t size, type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    auto& buffer = type == type::downstream ? *m_buffer_read_downstream : *m_buffer_read_upstream;
    buffer.set_size(size);
    do_write(type == type::downstream ? type::upstream : type::downstream);
  }
}

void server::impl::session_proxy::do_write(type type)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto& buffer            = type == type::downstream ? *m_buffer_read_upstream : *m_buffer_read_downstream;
  auto completion_handler = std::bind(&session_proxy::on_write, shared_from_this(), std::placeholders::_1, std::placeholders::_2, type);
  auto& socket            = type == type::downstream ? m_downstream_socket : m_upstream_socket;
  boost::asio::async_write(socket, core::helpers::const_memory_sequence(buffer), boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::session_proxy::on_write(const boost::system::error_code& error_code, std::size_t size, type type)
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
    do_read(type == type::downstream ? type::upstream : type::downstream);
  }
}

void server::impl::session_proxy::cancel_internal(const boost::system::error_code& error_code)
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

void server::impl::session_proxy::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::session_proxy::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected || !error_code) {
    return;
  }
  on_close(error_code);
}

void server::impl::session_proxy::on_close(const boost::system::error_code& error_code)
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
    m_downstream_socket.close(tmp);
    m_upstream_socket.close(tmp);
    m_resolver.cancel();
  }
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler                = nullptr;
  m_buffer_read_downstream = nullptr;
  m_buffer_read_upstream   = nullptr;
}

server::impl::session_exec::session_exec(socket_type&& downstream_socket, const settings_server& settings, const session_id_type& session_id, const exec& exec)
    : m_settings(settings)
{
  // TODO : Implement
}

void server::impl::session_exec::async_run(async_run_completion_handler&& handler)
{
  // TODO : Implement
}

void server::impl::session_exec::cancel()
{
  // TODO : Implement
}

}  // namespace ncat
}  // namespace rstream
