// See LICENSE file in the project root for license information.

#include "exposer.hpp"

#include <chrono>
#include <list>
#include <map>
#include <set>
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
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/bind_handler.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/write.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/object_id.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/stream.hpp>
#endif

#include "error.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace metrics {

class RSTREAM_GNUC_INTERNAL async_collector {
 public:
  using ptr = std::shared_ptr<async_collector>;

  virtual ~async_collector() = default;

  using async_collect_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&, const core::detail::metrics::metrics&)>;

  virtual void async_collect(const std::string& target, async_collect_completion_handler&& handler) = 0;
};

class RSTREAM_GNUC_INTERNAL exposer::impl : public async_collector, public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_exposer& settings);

  void add_collectable(core::metrics::collectable::ptr collectable, const std::string& target);

  void async_collect(const std::string& target, async_collect_completion_handler&& handler) override;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  class session;

  class session_proxy;

  class session_exec;

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

  struct internal_metrics {
    core::metrics::counter m_requests_total;
    core::metrics::histogram m_requests_duration_seconds;
    core::metrics::histogram m_response_size_bytes;
  };

  struct context {
    async_collector::ptr m_collector;
    internal_metrics& m_metrics;
  };

  enum class state {
    null     = 0,
    starting = 1,
    started  = 2,
    stopping = 3,
    stopped  = 4
  };

  using state_server_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  using collectables = std::map<std::string, std::list<core::metrics::collectable::ptr>>;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms);

  void add_collectable_internal(core::metrics::collectable::ptr collectable, const std::string& target);

  void async_collect_internal(const std::string& target, async_collect_completion_handler&& handler);

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

  const settings_exposer m_settings;

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

  collectables m_collectables;

  core::metrics::serializer m_serializer;

  internal_metrics m_metrics;
};

class RSTREAM_GNUC_INTERNAL exposer::impl::session : public std::enable_shared_from_this<session> {
 public:
  using ptr = std::shared_ptr<session>;

  session(socket_type&& socket, const session_id_type& session_id, const struct context& context);

  using async_run_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code&)>;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  using timestamp = core::detail::metrics::timestamp;

  enum class state {
    null         = 0,
    connected    = 1,
    disconnected = 2
  };

  void set_state(state state);

  void async_run_internal(async_run_completion_handler&& handler);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  void do_read_request();

  void on_read_request(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_process_request();

  void do_collect_metrics(const std::string& target);

  void on_metrics(const boost::system::error_code& error_code, const core::detail::metrics::metrics& metrics);

  void do_write_response();

  void on_write(bool close, const boost::system::error_code& error_code, std::size_t bytes_transferred);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  socket_type m_socket;

  const session_id_type m_session_id;

  const context m_context;

  rstream::core::logger m_logger;

  state m_state;

  boost::beast::flat_buffer m_buffer;

  boost::beast::http::request<boost::beast::http::string_body> m_request;

  boost::beast::http::response<boost::beast::http::string_body> m_response;

  timestamp m_request_timestamp;

  async_run_completion_handler m_handler;
};

exposer::exposer(const executor_type& executor, const config& config, const settings_exposer& settings)
    : io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
  add_collectable(rstream::core::metrics::default_registry());
}

exposer::~exposer() noexcept
{
  try {
    cancel();
  }
  catch (...) {
    return;
  }
}

void exposer::add_collectable(core::metrics::collectable::ptr collectable, const std::string& target)
{
  m_impl->add_collectable(collectable, target);
}

void exposer::async_run(async_run_completion_handler&& handler)
{
  m_impl->async_run(std::move(handler));
}

void exposer::cancel()
{
  m_impl->cancel();
}

exposer::impl::impl(const executor_type& executor, const config& config, const settings_exposer& settings)
    : m_executor(executor),
      m_strand(executor),
      m_config(config),
      m_settings(settings),
      m_logger({"rstream", "io", "dtl", "metrics", "exposer", fmt::format("#{}", fmt::ptr(this))}),
      m_acceptor(executor),
      m_socket(executor),
      m_state(state::null),
      m_resolver(executor),
      m_metrics({.m_requests_total = {"metrics_http_requests_total", "Counter of HTTP requests."}, .m_requests_duration_seconds = {"metrics_http_request_duration_seconds", "Histogram of latencies for HTTP requests."}, .m_response_size_bytes = {"metrics_http_response_size_bytes", "Histogram of response size for HTTP requests.", {}, nullptr, {1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9}}})
{
}

void exposer::impl::add_collectable(core::metrics::collectable::ptr collectable, const std::string& target)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::add_collectable_internal, shared_from_this(), collectable, target));
}

void exposer::impl::async_collect(const std::string& target, async_collect_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_collect_internal, shared_from_this(), target, std::move(handler)));
}

void exposer::impl::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), std::move(handler)));
}

void exposer::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::cancel_internal, shared_from_this()));
}

void exposer::impl::set_state(state state)
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

void exposer::impl::arm_state_timer(unsigned int timeout_ms)
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

void exposer::impl::async_collect_internal(const std::string& target, async_collect_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  boost::system::error_code error_code;
  core::detail::metrics::metrics metrics;
  auto it = m_collectables.find(target);
  if (it == m_collectables.end()) {
    error_code = error::code::no_data_available;
  }
  else {
    for (const auto& collectable : it->second) {
      collectable->collect(metrics);
    }
  }
  rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code, metrics);
}

void exposer::impl::async_run_internal(async_run_completion_handler&& handler)
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

void exposer::impl::do_resolve_host()
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

void exposer::impl::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
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

void exposer::impl::on_started()
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

void exposer::impl::do_accept()
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

void exposer::impl::on_accept(const boost::system::error_code& error_code)
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
      struct context context = {.m_collector = shared_from_this(), .m_metrics = m_metrics};
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

void exposer::impl::cancel_internal()
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

void exposer::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void exposer::impl::do_close(const boost::system::error_code& error_code)
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

void exposer::impl::on_close(const boost::system::error_code& error_code)
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

void exposer::impl::on_session_closed(const boost::system::error_code& error_code, const session_id_type& session_id)
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

exposer::impl::session_id_type exposer::impl::generate_session_id()
{
  return rstream::core::object_id();
}

void exposer::impl::add_collectable_internal(core::metrics::collectable::ptr collectable, const std::string& target)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto it = m_collectables.find(target);
  if (it == m_collectables.end()) {
    it = m_collectables.insert(std::make_pair(target, std::list<core::metrics::collectable::ptr>())).first;
  }
  it->second.push_back(collectable);
}

exposer::impl::session::session(socket_type&& socket, const session_id_type& session_id, const struct context& context)
    : m_executor(socket.get_executor()),
      m_strand(socket.get_executor()),
      m_socket(std::move(socket)),
      m_session_id(session_id),
      m_context(context),
      m_logger({"rstream", "io", "dtl", "metrics", "session", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null)
{
  std::stringstream str;
  boost::system::error_code error;
  str << m_socket.remote_endpoint(error);
  if (!error) {
    m_logger->debug("new connection from [{}]", str.str());
  }
}

void exposer::impl::session::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_run_internal, shared_from_this(), std::move(handler)));
}

void exposer::impl::session::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&exposer::impl::session::cancel_internal, shared_from_this(), boost::system::error_code()));
}

void exposer::impl::session::set_state(state state)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_state = state;
}

void exposer::impl::session::async_run_internal(async_run_completion_handler&& handler)
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

void exposer::impl::session::cancel_internal(const boost::system::error_code& error_code)
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

void exposer::impl::session::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void exposer::impl::session::on_close(const boost::system::error_code& error_code)
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

void exposer::impl::session::do_read_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_request               = {};
  m_response              = {};
  auto completion_handler = std::bind(&exposer::impl::session::on_read_request, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::beast::http::async_read(m_socket, m_buffer, m_request, boost::asio::bind_executor(m_strand, completion_handler));
}

void exposer::impl::session::on_read_request(const boost::system::error_code& error_code, std::size_t bytes_transferred)
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

void exposer::impl::session::do_process_request()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_request_timestamp = timestamp::clock::now();
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
  m_response.version(m_request.version());
  m_response.keep_alive(m_request.keep_alive());
  auto bad_request = (m_request.method() != boost::beast::http::verb::get);
  if (bad_request) {
    m_response.result(boost::beast::http::status::bad_request);
  }
  if (bad_request) {
    do_write_response();
  }
  else {
    do_collect_metrics(m_request.target());
  }
}

void exposer::impl::session::do_collect_metrics(const std::string& target)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&session::on_metrics, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_context.m_collector->async_collect(target, boost::asio::bind_executor(m_strand, completion_handler));
}

void exposer::impl::session::on_metrics(const boost::system::error_code& error_code, const core::detail::metrics::metrics& metrics)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (error_code) {
    if (error_code == error::code::no_data_available) {
      m_response.result(boost::beast::http::status::not_found);
    }
    else {
      m_response.result(boost::beast::http::status::internal_server_error);
    }
  }
  else {
    m_response.result(boost::beast::http::status::ok);
    m_response.set(boost::beast::http::field::content_type, "text/plain");
    m_response.body() = rstream::core::metrics::serializer()(metrics);
  }
  do_write_response();
}

void exposer::impl::session::do_write_response()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_response.result() != boost::beast::http::status::ok) {
    m_response.set(boost::beast::http::field::content_type, "text/plain");
    {
      std::stringstream str;
      str << m_response.result();
      m_response.body() = str.str();
    }
  }
  m_response.prepare_payload();
#ifdef DEBUG_BUILD
  {
    std::stringstream str;
    str << m_response.base();
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("HTTP response :\n{}", str.str());
  }
#else
  m_logger->trace("HTTP response {}", m_response.result_int());
#endif
  boost::beast::http::async_write(m_socket, m_response, boost::asio::bind_executor(m_strand, boost::beast::bind_front_handler(&exposer::impl::session::on_write, shared_from_this(), m_response.need_eof())));
}

void exposer::impl::session::on_write(bool close, const boost::system::error_code& error_code, std::size_t bytes_transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (error_code) {
    on_error(error_code);
  }
  else {
    {
      core::detail::metrics::labels labels = {{"handler", m_request.target()}};
      if (m_response.result() == boost::beast::http::status::ok) {
        auto delay_s = std::chrono::duration_cast<std::chrono::microseconds>(timestamp::clock::now() - m_request_timestamp).count() / 1000000.0;
        m_context.m_metrics.m_requests_duration_seconds.labels(labels).observe(delay_s);
        m_context.m_metrics.m_response_size_bytes.labels(labels).observe(bytes_transferred);
      }
      labels.insert({"code", std::to_string((int)m_response.result())});
      m_context.m_metrics.m_requests_total.labels(labels).increment();
    }
    if (close) {
      // this means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic
      on_close(boost::system::error_code());
    }
    else {
      do_read_request();
    }
  }
}

}  // namespace metrics
}  // namespace detail
}  // namespace io
}  // namespace rstream
