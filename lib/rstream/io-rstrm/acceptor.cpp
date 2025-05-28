// See LICENSE file in the project root for license information.

#include "acceptor.hpp"

#include <mutex>

#include <boost/algorithm/string.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>

#include "client.hpp"
#include "error.hpp"
#include "tunnel.hpp"

namespace rstream {
namespace io_rstrm {

class RSTREAM_GNUC_INTERNAL acceptor::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, core::allocator::ptr allocator);

  virtual ~impl() = default;

  void set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code);

  settings_acceptor settings(boost::system::error_code& error_code);

  void settings(const settings_acceptor& settings, boost::system::error_code& error_code);

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  void close(boost::system::error_code& error_code);

  void bind(const endpoint& endpoint, boost::system::error_code& error_code);

  void listen(int backlog, boost::system::error_code& error_code);

  endpoint local_endpoint(boost::system::error_code& error_code);

  void async_accept(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

 private:
  enum class state {
    null       = 0,
    idle       = 1,
    connecting = 2,
    connected  = 3,
  };

  struct accept_op {
    using ptr = std::shared_ptr<accept_op>;
    accept_op(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);
    socket& m_peer;
    endpoint& m_endpoint;
    async_accept_completion_handler m_handler;
  };

  void async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  void do_connect();

  void do_reconnect();

  void on_connect(const boost::system::error_code& error_code);

  void do_create_tunnel();

  void do_recreate_tunnel();

  void on_create_tunnel(const boost::system::error_code& error_code, tunnel tunnel);

  void do_accept();

  void on_accept(const boost::system::error_code& error_code);

  void on_timer_cb(const boost::system::error_code& error_code);

  void on_client_disconnection(const boost::system::error_code& error_code);

  void on_client_status(const status& status);

  void close_internal();

  void on_error(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  settings_acceptor m_settings;

  core::logger m_logger;

  state m_state;

  bool m_closing;

  core::allocator::ptr m_allocator;

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  boost::asio::deadline_timer m_timer;

  std::shared_ptr<client> m_client;

  tunnel m_tunnel;

  endpoint m_endpoint;

  accept_op::ptr m_accept_op;

  std::mutex m_mutex;

  bool m_is_state_non_null;

  control_callbacks m_control_callbacks;

  boost::optional<endpoint> m_local_endpoint;

  status_extd m_status;
};

acceptor::impl::accept_op::accept_op(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
    : m_peer(peer),
      m_endpoint(endpoint),
      m_handler(std::move(handler))
{
}

acceptor::acceptor(const io_object::executor_type& executor, core::allocator::ptr allocator)
    : io::acceptor_base<endpoint, socket>(executor)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), executor, allocator);
}

acceptor::acceptor(const io_object::executor_type& executor, const settings_acceptor& settings, core::allocator::ptr allocator)
    : acceptor(executor, allocator)
{
  boost::system::error_code error_code;
  this->settings(settings, error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
}

void acceptor::set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code)
{
  return m_impl->set_control_callbacks(callbacks, error_code);
}

settings_acceptor acceptor::settings(boost::system::error_code& error_code) const
{
  return m_impl->settings(error_code);
}

void acceptor::settings(const settings_acceptor& settings, boost::system::error_code& error_code)
{
  return m_impl->settings(settings, error_code);
}

void acceptor::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->open(endpoint, error_code);
}

void acceptor::close(boost::system::error_code& error_code)
{
  return m_impl->close(error_code);
}

void acceptor::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  return m_impl->bind(endpoint, error_code);
}

void acceptor::listen(int backlog, boost::system::error_code& error_code)
{
  return m_impl->listen(backlog, error_code);
}

endpoint acceptor::local_endpoint(boost::system::error_code& error_code)
{
  return m_impl->local_endpoint(error_code);
}

void acceptor::async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  return m_impl->async_accept(peer, endpoint, std::move(handler));
}

acceptor::impl::impl(const executor_type& executor, core::allocator::ptr allocator)
    : m_logger({"rstream", "io-rstrm", "acceptor", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null),
      m_closing(false),
      m_allocator(allocator),
      m_executor(executor),
      m_strand(executor),
      m_timer(executor),
      m_is_state_non_null(false)
{
}

void acceptor::impl::set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  else {
    m_control_callbacks = callbacks;
  }
}

settings_acceptor acceptor::impl::settings(boost::system::error_code& error_code)
{
  (void)error_code;
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_settings;
}

void acceptor::impl::settings(const settings_acceptor& settings, boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  else {
    m_settings = settings;
  }
}

void acceptor::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  (void)endpoint;
  (void)error_code;
}

void acceptor::impl::close(boost::system::error_code& error_code)
{
  (void)error_code;
  boost::asio::dispatch(m_strand, std::bind_front(&impl::close_internal, shared_from_this()));
}

void acceptor::impl::bind(const endpoint& endpoint, boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  else {
    m_local_endpoint = endpoint;
  }
}

void acceptor::impl::listen(int backlog, boost::system::error_code& error_code)
{
  (void)backlog;
  (void)error_code;
}

endpoint acceptor::impl::local_endpoint(boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_local_endpoint) {
    return m_local_endpoint.get();
  }
  error_code = error::code::invalid_state;
  return endpoint();
}

void acceptor::impl::async_accept(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_accept_internal, shared_from_this(), std::ref(peer), std::ref(endpoint), std::move(handler)));
}

void acceptor::impl::async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  boost::system::error_code error_code;
  if (m_accept_op) {
    error_code = error::code::operation_in_progress;
  }
  else if (m_state == state::null) {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      if (m_local_endpoint) {
        m_endpoint          = m_local_endpoint.get();
        m_is_state_non_null = true;
      }
      else {
        error_code = error::code::no_valid_endpoint;
      }
    }
    if (!error_code) {
      m_state = state::idle;
    }
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code);
  }
  else {
    if (m_client == nullptr) {
      m_client = std::allocate_shared<client>(core::allocator::wrapper<impl>(m_allocator), m_executor, m_settings.m_config, m_allocator);
    }
    m_accept_op = std::allocate_shared<accept_op>(core::allocator::wrapper<impl>(m_allocator), peer, endpoint, std::move(handler));
    if (m_state == state::idle) {
      do_connect();
    }
    else if (m_tunnel) {
      do_accept();
    }
  }
}

void acceptor::impl::do_connect()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::idle) {
    return;
  }
  m_state                = state::connecting;
  auto control_callbacks = (client::control_callbacks){
      .m_on_disconnection_cb = [ptr = shared_from_this()](const boost::system::error_code& error_code) { boost::asio::dispatch(ptr->m_strand, std::bind(&acceptor::impl::on_client_disconnection, ptr, error_code)); },
      .m_on_status_cb        = [ptr = shared_from_this()](const status& status) { boost::asio::dispatch(ptr->m_strand, std::bind(&acceptor::impl::on_client_status, ptr, status)); },
  };
  boost::system::error_code error_code;
  m_client->set_control_callbacks(control_callbacks, error_code);
  if (error_code) {
    on_error(error_code);
  }
  else {
    m_logger->trace("connecting to the engine server...");
    auto completion_handler = std::bind(&impl::on_connect, shared_from_this(), std::placeholders::_1);
    m_client->async_connect(m_endpoint.m_server_address, boost::asio::bind_executor(m_strand, completion_handler));
  }
}

void acceptor::impl::do_reconnect()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::idle) {
    return;
  }
  m_timer.expires_from_now(boost::posix_time::milliseconds(m_settings.m_reconnect_timeout_ms));
  auto completion_handler = std::bind(&impl::on_timer_cb, shared_from_this(), std::placeholders::_1);
  m_timer.async_wait(boost::asio::bind_executor(m_strand, completion_handler));
}

void acceptor::impl::on_connect(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  if (m_closing) {
    on_close(boost::system::error_code());
  }
  else if (error_code) {
    m_logger->trace("an error occurred while connecting to the engine server [error_code: {}]", error_code.message());
    if (m_settings.m_auto_reconnect) {
      m_state  = state::idle;
      m_status = status_extd{
          status{},
          std::string("connection failed (" + boost::algorithm::to_lower_copy(error_code.message()) + ")"),
          {},
          {},
      };
      if (m_control_callbacks.m_on_status_cb) {
        m_control_callbacks.m_on_status_cb(m_status);
      }
      do_reconnect();
    }
    else {
      on_error(error_code);
    }
  }
  else {
    m_logger->trace("client connected successfully");
    m_state  = state::connected;
    m_status = status_extd{
        status{m_status},
        std::string("connected"),
        {},
        {},
    };
    if (m_control_callbacks.m_on_status_cb) {
      m_control_callbacks.m_on_status_cb(m_status);
    }
    do_create_tunnel();
  }
}

void acceptor::impl::do_create_tunnel()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (m_closing) {
    return;
  }
  m_logger->trace("creating tunnel...");
  auto completion_handler      = std::bind(&impl::on_create_tunnel, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  tunnel_properties properties = m_settings.m_tunnel_properties;
  if (m_endpoint.m_id_name) {
    if (properties.m_name) {
#ifdef DEBUG_BUILD
      m_logger->trace("tunnel name will be overridden");
#endif
    }
    properties.m_name = m_endpoint.m_id_name;
  }
  m_client->async_create_tunnel(properties, boost::asio::bind_executor(m_strand, completion_handler));
}

void acceptor::impl::do_recreate_tunnel()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  m_timer.expires_from_now(boost::posix_time::milliseconds(m_settings.m_recreate_tunnel_timeout_ms));
  auto completion_handler = std::bind(&impl::on_timer_cb, shared_from_this(), std::placeholders::_1);
  m_timer.async_wait(boost::asio::bind_executor(m_strand, completion_handler));
}

void acceptor::impl::on_create_tunnel(const boost::system::error_code& error_code, tunnel tunnel)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  tunnel_properties properties;
  boost::system::error_code cause = error_code;
  if (!cause) {
    properties = tunnel.properties(cause);
  }
  if (cause) {
    m_logger->trace("an error occurred while creating tunnel [error_code: {}]", cause.message());
    if (m_settings.m_auto_recreate_tunnel) {
      do_recreate_tunnel();
    }
    else {
      on_error(cause);
    }
  }
  else {
    m_tunnel = std::move(tunnel);
    if (!properties.m_id) {
      m_logger->warn("tunnel ID is not available");
    }
    else if (m_control_callbacks.m_on_tunnel_properties_cb) {
      m_control_callbacks.m_on_tunnel_properties_cb(properties);
    }
    m_status = status_extd{
        status{m_status},
        std::string("online"),
        properties.m_id,
        {},
    };
    const auto& forwarding = format_forwarding_address(properties);
    if (forwarding) {
      m_status.m_forwarding = forwarding.value();
    }
    m_logger->trace("tunnel created successfully [tunnel_id={}, forwarding={}]",
                    properties.m_id.get_value_or("undefined"),
                    m_status.m_forwarding.get_value_or("undefined"));
    if (m_control_callbacks.m_on_status_cb) {
      m_control_callbacks.m_on_status_cb(m_status);
    }
    if (m_accept_op) {
      do_accept();
    }
  }
}

void acceptor::impl::do_accept()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  auto completion_handler = std::bind(&impl::on_accept, shared_from_this(), std::placeholders::_1);
  m_tunnel.async_accept(m_accept_op->m_peer, m_accept_op->m_endpoint, boost::asio::bind_executor(m_strand, completion_handler));
}

void acceptor::impl::on_accept(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (m_closing) {
    return;
  }
  if (error_code) {
    m_tunnel = nullptr;
    if (m_settings.m_auto_recreate_tunnel) {
      m_logger->trace("tunnel ended unexpectedly [error_code: {}]", error_code.message());
      m_status = status_extd{
          status{m_status},
          std::string("connected"),
          {},
          {},
      };
      if (m_control_callbacks.m_on_status_cb) {
        m_control_callbacks.m_on_status_cb(m_status);
      }
      do_recreate_tunnel();
    }
    else {
      on_error(error_code);
    }
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_accept_op->m_handler), error_code);
    m_accept_op = nullptr;
  }
}

void acceptor::impl::on_timer_cb(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (error_code || m_closing) {
    return;
  }
  if (m_state == state::idle) {
    do_connect();
  }
  else if (m_state == state::connected) {
    do_create_tunnel();
  }
}

void acceptor::impl::on_client_disconnection(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  if (m_closing || !error_code) {
    on_close(error_code);
  }
  else if (m_settings.m_auto_reconnect) {
    m_logger->trace("connection to the engine server closed unexpectedly [error_code: {}]", error_code.message());
    m_state = state::idle;
    m_timer.cancel();
    m_tunnel = nullptr;
    m_status = status_extd{
        status{},
        std::string("disconnected (" + boost::algorithm::to_lower_copy(error_code.message()) + ")"),
        {},
        {},
    };
    if (m_control_callbacks.m_on_status_cb) {
      m_control_callbacks.m_on_status_cb(m_status);
    }
    do_reconnect();
  }
  else if (error_code) {
    on_error(error_code);
  }
}

void acceptor::impl::on_client_status(const status& status_)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  m_status = status_extd{
      status{status_},
      m_status.m_status,
      m_status.m_tunnel_id,
      m_status.m_forwarding,
  };
  if (m_control_callbacks.m_on_status_cb) {
    m_control_callbacks.m_on_status_cb(m_status);
  }
}

void acceptor::impl::close_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("closing acceptor...");
#endif
  if (m_state == state::idle) {
    on_close(boost::system::error_code());
  }
  else if (!m_closing) {
    m_closing = true;
    m_client->close();
  }
}

void acceptor::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void acceptor::impl::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("acceptor closed");
#endif
  m_state   = state::null;
  m_closing = false;
  m_tunnel  = nullptr;
  m_status  = {};
  {
    boost::system::error_code tmp;
    m_timer.cancel(tmp);
  }
  m_client->close();
  m_client = nullptr;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_local_endpoint.reset();
    m_is_state_non_null = false;
    m_control_callbacks = {};
  }
  if (m_accept_op) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_accept_op->m_handler), error_code ? error_code : error::code::operation_aborted);
    m_accept_op = nullptr;
  }
}

}  // namespace io_rstrm
}  // namespace rstream
