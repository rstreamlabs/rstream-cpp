// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>

#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/algorithm/string/trim.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/optional.hpp>
#include <boost/signals2.hpp>
#include <boost/system/system_error.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/object_id.hpp>
#include <rstream/io/payloader.hpp>
#include <rstream/io/queue.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/stream.hpp>
#endif
#include <rstream/io-rstrm/protobuf/messages.pb.h>

#include "detail/convert.hpp"
#include "error.hpp"

namespace rstream {
namespace io_rstrm {

static bool invalid_published_tcp_options(const tunnel_properties& properties)
{
  const bool published_tcp = properties.m_protocol && properties.m_protocol.value() == protocol::tcp;
  if (properties.m_allow_cross_region_routing && !published_tcp) {
    return true;
  }
  if (properties.m_port && !published_tcp) {
    return true;
  }
  if (!published_tcp) {
    return false;
  }
  if ((properties.m_publish && !properties.m_publish.value())
      || (properties.m_type && properties.m_type.value() != "bytestream")
      || (properties.m_port && (properties.m_port.value() == 0 || properties.m_port.value() > 65535))) {
    return true;
  }
  return properties.m_hostname
         || properties.m_tls_mode
         || !properties.m_tls_alpns.empty()
         || properties.m_tls_min_version
         || !properties.m_tls_ciphers.empty()
         || properties.m_mtls_auth
         || properties.m_http_version
         || properties.m_http_use_tls
         || properties.m_upstream_tls
         || properties.m_token_auth
         || properties.m_rstream_auth
         || properties.m_challenge_mode
         || properties.m_datagram_guaranteed_delivery;
}

static tunnel_properties normalize_tunnel_properties(const tunnel_properties& properties)
{
  auto normalized = properties;
  if (normalized.m_protocol && normalized.m_protocol.value() == protocol::tcp) {
    if (!normalized.m_type) {
      normalized.m_type = "bytestream";
    }
    if (!normalized.m_publish) {
      normalized.m_publish = true;
    }
  }
  return normalized;
}

class RSTREAM_GNUC_INTERNAL tunnel::impl : public std::enable_shared_from_this<impl> {
 public:
  using client_type = std::shared_ptr<client::impl>;

  impl(const std::string& tunnel_id, const endpoint& local_endpoint, const tunnel_properties& tunnel_properties, client_type client);

  virtual ~impl() = default;

  endpoint local_endpoint(boost::system::error_code& error_code);

  tunnel_properties properties(boost::system::error_code& error_code);

  void async_accept(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler);

  void close();

 private:
  const std::string m_tunnel_id;

  const endpoint m_local_endpoint;

  const tunnel_properties m_tunnel_properties;

  client_type m_client;
};

class RSTREAM_GNUC_INTERNAL client::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config_client& config, core::allocator::ptr allocator);

  virtual ~impl() = default;

  io::address address(boost::system::error_code& error_code);

  void set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code);

  void async_connect(const io::address& address, async_connect_completion_handler&& handler);

  void async_connect(async_connect_completion_handler&& handler);

  void async_create_tunnel(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler);

  void async_accept_tunnel(const std::string& tunnel_id, socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler);

  void close_tunnel(const std::string& tunnel_id);

  void close();

 private:
#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = io::stream;
#else
  using protocol_type = boost::asio::ip::tcp;
#endif

  using resolver_type = protocol_type::resolver;

  using socket_type = protocol_type::socket;

  using payloader_type = io::payloader<socket_type&>;

  using queue_type = io::queue<payloader_type&>;

  struct create_tunnel_op_type {
    using ptr        = std::shared_ptr<create_tunnel_op_type>;
    using request_id = std::string;
    create_tunnel_op_type(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler);
    const tunnel_properties m_properties;
    async_create_tunnel_completion_handler m_handler;
  };

  using create_tunnel_ops_type = std::map<create_tunnel_op_type::request_id, create_tunnel_op_type::ptr>;

  struct accept_tunnel_op_type {
    using ptr = std::shared_ptr<accept_tunnel_op_type>;
    accept_tunnel_op_type(socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler);
    bool m_busy;
    socket& m_peer;
    endpoint& m_endpoint;
    tunnel::async_accept_completion_handler m_handler;
  };

  struct stream_type {
    using ptr = std::shared_ptr<stream_type>;
    using id  = std::string;
    struct context {
      boost::optional<std::string> m_secret;
      boost::optional<boost::asio::ip::address> m_source_ip;
      boost::optional<io::address> m_proxy_endpoint;
    };
    struct container {
      ptr m_ptr;
      context m_context;
    };
    stream_type(const executor_type& executor, core::allocator::ptr allocator);
    bool m_connected;
    socket m_peer;
  };

  using streams_type = std::map<stream_type::id, stream_type::container>;

  struct tunnel_type {
    using ptr = std::shared_ptr<tunnel_type>;
    using id  = std::string;
    tunnel_type();
    bool m_closing;
    accept_tunnel_op_type::ptr m_accept_tunnel_op;
    streams_type m_streams;
  };

  using tunnels_type = std::map<tunnel_type::id, tunnel_type::ptr>;

  enum class state {
    null       = 0,
    connecting = 1,
    connected  = 2,
    closing    = 3,
  };

  using state_server_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void arm_state_timer(unsigned int timeout_ms);

  void async_connect_internal(const io::address& address, async_connect_completion_handler&& handler);

  void async_create_tunnel_internal(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler);

  void async_accept_tunnel_internal(const tunnel_type::id& tunnel_id, socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler);

  void close_tunnel_internal(const tunnel_type::id& tunnel_id);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_connect(const resolver_type::results_type& results);

  void on_connect(const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint);

  void do_open();

  void do_read();

  void on_open();

  void do_create_tunnel(create_tunnel_ops_type::iterator create_tunnel_op_it);

  void on_tunnel_response(create_tunnel_ops_type::iterator& create_tunnel_op_it, const tunnel_properties& tunnel_properties, const boost::system::error_code& error_code);

  void do_close_tunnel(const tunnel_type::id& tunnel_id);

  void on_tunnel_close(tunnels_type::iterator& tunnel_it);

  void on_proxy_request(tunnels_type::iterator& tunnel_it, const stream_type::id& stream_id, const stream_type::context& context);

  void do_send_proxy_response(const stream_type::id& stream_id, const boost::system::error_code& error_code);

  void do_connect_stream(tunnels_type::iterator& tunnel_it, streams_type::iterator& stream_it);

  void on_connect_stream(const tunnel_type::id& tunnel_id, const stream_type::id& stream_id, const boost::system::error_code& error_code);

  void do_read_incoming_message();

  void on_read_incoming_data(const boost::system::error_code& error_code);

  void on_read_incoming_message(const protobuf::Message& message);

  using on_send_message_cb = std::function<void()>;

  void do_send_message(const protobuf::Message& message, const on_send_message_cb& cb = nullptr);

  void on_send_message(const boost::system::error_code& error_code, const on_send_message_cb& cb);

  void close_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  void send_heartbeat();

  void do_send_heartbeat();

  static std::string generate_request_id();

  static bool is_message_expected(state state, const protobuf::Message& message);

  config_client m_config;

  core::logger m_logger;

  state m_state;

  core::allocator::ptr m_allocator;

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  resolver_type m_resolver;

  socket_type m_socket;

  payloader_type m_payloader;

  queue_type m_queue;

  std::mutex m_mutex;

  bool m_is_state_non_null;

  io::address m_server_address;

  control_callbacks m_control_callbacks;

  core::buffer m_buffer;

  async_connect_completion_handler m_handler;

  create_tunnel_ops_type m_create_tunnel_ops;

  tunnels_type m_tunnels;

  boost::system::error_code m_error_code;

  client_details m_client_details;

  status m_status;

  state_server_changed_signal_type m_state_changed_signal;
};

tunnel::tunnel(std::nullptr_t)
    : m_impl(nullptr)
{
}

tunnel::tunnel()
    : tunnel(nullptr)
{
}

tunnel::tunnel(std::shared_ptr<impl> impl)
    : m_impl(std::move(impl))
{
}

tunnel::operator bool() const noexcept
{
  return m_impl != nullptr;
}

endpoint tunnel::local_endpoint(boost::system::error_code& error_code)
{
  return m_impl->local_endpoint(error_code);
}

endpoint tunnel::local_endpoint()
{
  endpoint endpoint;
  boost::system::error_code error_code;
  endpoint = local_endpoint(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return endpoint;
}

tunnel_properties tunnel::properties(boost::system::error_code& error_code)
{
  return m_impl->properties(error_code);
}

tunnel_properties tunnel::properties()
{
  tunnel_properties tunnel_properties;
  boost::system::error_code error_code;
  tunnel_properties = properties(error_code);
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
  return tunnel_properties;
}

void tunnel::async_accept_internal(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  return m_impl->async_accept(peer, endpoint, std::move(handler));
}

void tunnel::close()
{
  return m_impl->close();
}

client::client(const executor_type& executor, const config_client& config, core::allocator::ptr allocator)
    : io::io_object(executor)
{
  m_impl = std::allocate_shared<impl>(core::allocator::wrapper<impl>(allocator), executor, config, allocator);
}

client::client(const executor_type& executor, core::allocator::ptr allocator)
    : client(executor, config_client(), allocator)
{
}

io::address client::address(boost::system::error_code& error_code) const
{
  return m_impl->address(error_code);
}

void client::set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code)
{
  return m_impl->set_control_callbacks(callbacks, error_code);
}

void client::close()
{
  return m_impl->close();
}

void client::async_connect_internal(const io::address& address, async_connect_completion_handler&& handler)
{
  return m_impl->async_connect(address, std::move(handler));
}

void client::async_connect_internal(async_connect_completion_handler&& handler)
{
  return m_impl->async_connect(std::move(handler));
}

void client::async_create_tunnel_internal(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler)
{
  return m_impl->async_create_tunnel(properties, std::move(handler));
}

tunnel::impl::impl(const std::string& tunnel_id, const endpoint& local_endpoint, const tunnel_properties& tunnel_properties, client_type client)
    : m_tunnel_id(tunnel_id),
      m_local_endpoint(local_endpoint),
      m_tunnel_properties(tunnel_properties),
      m_client(client)
{
}

endpoint tunnel::impl::local_endpoint(boost::system::error_code& error_code)
{
  (void)error_code;
  return m_local_endpoint;
}

tunnel_properties tunnel::impl::properties(boost::system::error_code& error_code)
{
  (void)error_code;
  return m_tunnel_properties;
}

void tunnel::impl::async_accept(socket& peer, endpoint& endpoint, async_accept_completion_handler&& handler)
{
  m_client->async_accept_tunnel(m_tunnel_id, peer, endpoint, std::move(handler));
}

void tunnel::impl::close()
{
  m_client->close_tunnel(m_tunnel_id);
}

client::impl::create_tunnel_op_type::create_tunnel_op_type(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler)
    : m_properties(properties),
      m_handler(std::move(handler))
{
}

client::impl::accept_tunnel_op_type::accept_tunnel_op_type(socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler)
    : m_busy(false),
      m_peer(peer),
      m_endpoint(endpoint),
      m_handler(std::move(handler))
{
}

client::impl::stream_type::stream_type(const executor_type& executor, core::allocator::ptr allocator)
    : m_connected(false),
      m_peer(executor, allocator)
{
}

client::impl::tunnel_type::tunnel_type()
    : m_closing(false)
{
}

client::impl::impl(const executor_type& executor, const config_client& config, core::allocator::ptr allocator)
    : m_config(config),
      m_logger({"rstream", "io-rstrm", "client", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null),
      m_allocator(allocator),
      m_executor(executor),
      m_strand(executor),
      m_resolver(executor),
      m_socket(executor),
      m_payloader(m_socket, allocator),
      m_queue(m_payloader, allocator),
      m_is_state_non_null(false),
      m_buffer(allocator)
{
}

io::address client::impl::address(boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  return m_server_address;
}

void client::impl::set_control_callbacks(const control_callbacks& callbacks, boost::system::error_code& error_code)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_is_state_non_null) {
    error_code = error::code::invalid_state;
  }
  else {
    m_control_callbacks = callbacks;
  }
}

void client::impl::async_connect(const io::address& address, async_connect_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_connect_internal, shared_from_this(), address, std::move(handler)));
}

void client::impl::async_connect(async_connect_completion_handler&& handler)
{
  auto server_result = get_rstream_engine_address(m_config.m_config_path);
  if (server_result) {
    return async_connect(server_result.value(), std::move(handler));
  }
  else {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), server_result.error());
  }
}

void client::impl::async_create_tunnel(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_create_tunnel_internal, shared_from_this(), properties, std::move(handler)));
}

void client::impl::async_accept_tunnel(const std::string& tunnel_id, socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_accept_tunnel_internal, shared_from_this(), tunnel_id, std::ref(peer), std::ref(endpoint), std::move(handler)));
}

void client::impl::close_tunnel(const std::string& tunnel_id)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::close_tunnel_internal, shared_from_this(), tunnel_id));
}

void client::impl::close()
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::close_internal, shared_from_this(), boost::system::error_code()));
}

void client::impl::set_state(state state)
{
  m_state = state;
  m_state_changed_signal(state);
  std::stringstream str;
  switch (state) {
    case state::null:
      str << "null";
      break;
    case state::connecting:
      str << "connecting";
      break;
    case state::connected:
      str << "connected";
      break;
    case state::closing:
      str << "closing";
      break;
    default:
      break;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("client state changed to '{}'", str.str());
#endif
}

void client::impl::arm_state_timer(unsigned int timeout_ms)
{
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

void client::impl::async_connect_internal(const io::address& address, async_connect_completion_handler&& handler)
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
  else {
    m_server_address = address;
    m_handler.swap(handler);
    set_state(state::connecting);
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_is_state_non_null = true;
    }
    arm_state_timer(m_config.m_connection_timeout_ms);
    do_resolve_host();
  }
}

void client::impl::async_create_tunnel_internal(const tunnel_properties& properties, async_create_tunnel_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  if (m_state != state::connected) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state, nullptr);
  }
  else {
    const auto normalized = normalize_tunnel_properties(properties);
    if ((normalized.m_type && normalized.m_type.value() != "bytestream") || invalid_published_tcp_options(normalized)) {
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_configuration, nullptr);
      return;
    }
    const auto request_id       = generate_request_id();
    const auto create_tunnel_op = std::allocate_shared<create_tunnel_op_type>(core::allocator::wrapper<impl>(m_allocator), normalized, std::move(handler));
    do_create_tunnel(m_create_tunnel_ops.insert(std::make_pair(request_id, create_tunnel_op)).first);
  }
}

void client::impl::async_accept_tunnel_internal(const tunnel_type::id& tunnel_id, socket& peer, endpoint& endpoint, tunnel::async_accept_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!handler) {
    return;
  }
  tunnels_type::iterator tunnel_it;
  boost::system::error_code error_code;
  {
    if (m_state != state::connected) {
      error_code = error::code::invalid_state;
    }
    else {
      tunnel_it = m_tunnels.find(tunnel_id);
      if (tunnel_it == m_tunnels.end()) {
        error_code = error::code::tunnel_not_found;
      }
      else if (tunnel_it->second->m_accept_tunnel_op) {
        error_code = error::code::operation_in_progress;
      }
    }
  }
  if (error_code) {
    rstream::core::invoke_completion_handler(m_executor, std::move(handler), error_code);
  }
  else {
    if (peer.m_impl) {
#ifdef DEBUG_BUILD
      m_logger->trace("accept tunnel called for an already initialized peer [tunnel_id={}]", tunnel_id);
#endif
      boost::system::error_code tmp;
      peer.close(tmp);
    }
    streams_type::iterator stream_it = tunnel_it->second->m_streams.end();
    if (m_config.m_async_stream_operation) {
      for (stream_it = tunnel_it->second->m_streams.begin(); stream_it != tunnel_it->second->m_streams.end(); ++stream_it) {
        if (stream_it->second.m_ptr && stream_it->second.m_ptr->m_connected) {
          break;
        }
      }
    }
    if (stream_it != tunnel_it->second->m_streams.end()) {
      peer               = std::move(stream_it->second.m_ptr->m_peer);  // TODO : Properly rebind executor
      const auto address = stream_it->second.m_context.m_proxy_endpoint ? stream_it->second.m_context.m_proxy_endpoint.get() : m_server_address;
      endpoint           = (struct endpoint){.m_id_name = stream_it->first, .m_server_address = address, .m_secret = boost::none, .m_source_ip = stream_it->second.m_context.m_source_ip};
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), boost::system::error_code());
#ifdef DEBUG_BUILD
      m_logger->trace("accept operation completed [tunnel_id={}, stream_id={}]", tunnel_id, stream_it->first);
#endif
      stream_it = tunnel_it->second->m_streams.erase(stream_it);
    }
    else {
      tunnel_it->second->m_accept_tunnel_op = std::allocate_shared<accept_tunnel_op_type>(core::allocator::wrapper<impl>(m_allocator), peer, endpoint, std::move(handler));
      if (!m_config.m_async_stream_operation) {
        for (stream_it = tunnel_it->second->m_streams.begin(); stream_it != tunnel_it->second->m_streams.end(); ++stream_it) {
          if (stream_it->second.m_ptr == nullptr) {
            break;
          }
        }
        if (stream_it != tunnel_it->second->m_streams.end()) {
          do_connect_stream(tunnel_it, stream_it);
        }
      }
    }
  }
}

void client::impl::close_tunnel_internal(const tunnel_type::id& tunnel_id)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  tunnels_type::value_type::second_type tunnel;
  boost::system::error_code error_code;
  {
    tunnels_type::iterator it;
    if (m_state != state::connected) {
      error_code = error::code::invalid_state;
    }
    else {
      it = m_tunnels.find(tunnel_id);
      if (it == m_tunnels.end()) {
        error_code = error::code::tunnel_not_found;
      }
      else if (it->second->m_closing) {
        error_code = error::code::operation_in_progress;
      }
      else {
        tunnel = it->second;
      }
    }
  }
  if (!error_code) {
    tunnel->m_closing = true;
    do_close_tunnel(tunnel_id);
  }
}

void client::impl::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_server_address.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_server_address.host(), m_server_address.port(), boost::asio::bind_executor(m_strand, completion_handler));
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

void client::impl::do_connect(const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&impl::on_connect, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  boost::asio::async_connect(m_socket, results, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_connect(const boost::system::error_code& error_code, const resolver_type::results_type::endpoint_type& endpoint)
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
    do_read();
  }
}

void client::impl::do_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  protobuf::Message message;
  boost::system::error_code error_code;
  {
    protobuf::OpenControlChannelReq payload;
    protobuf::ClientDetails proto_client_details;
    auto client_details = get_client_details(m_config, m_server_address);
    if (client_details) {
      m_client_details = client_details.value();
      detail::convert(proto_client_details, client_details.value());
    }
    else {
      error_code = client_details.error();
    }
    if (!error_code) {
      bool is_secure = false;
#ifdef RSTREAM_WITH_IO_STREAMS
      is_secure = m_socket.is_secure();
#endif
      if (proto_client_details.has_token()) {
        if (!is_secure) {
#ifdef DEBUG_BUILD
          m_logger->warn("authentication token must not be sent over insecure connection");
#endif
          error_code = error::code::protocol_error;
        }
      }
      else {
        m_logger->info("no authentication token provided, please generate one on adminisration panel");
      }
    }
    if (!error_code) {
      payload.mutable_client_details()->CopyFrom(proto_client_details);
      message.mutable_open_control_channel_req()->CopyFrom(payload);
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_send_message(message);
  }
}

void client::impl::do_read()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer.append(core::make_memory_allocated(m_config.m_max_buffer_size, m_allocator));
  do_read_incoming_message();
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
  rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), boost::system::error_code());
  m_handler = nullptr;
  if (m_control_callbacks.m_on_status_cb) {
    m_control_callbacks.m_on_status_cb(m_status);
  }
  if (m_config.m_hearbeat) {
    send_heartbeat();
  }
}

void client::impl::do_create_tunnel(create_tunnel_ops_type::iterator create_tunnel_op_it)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  protobuf::Message message;
  protobuf::OpenTunnelReq payload;
  payload.set_request_id(create_tunnel_op_it->first);
  protobuf::TunnelProperties tunnel_properties;
  detail::convert(tunnel_properties, create_tunnel_op_it->second->m_properties);
  payload.mutable_tunnel_properties()->CopyFrom(tunnel_properties);
  message.mutable_open_tunnel_req()->CopyFrom(payload);
  do_send_message(message);
}

void client::impl::on_tunnel_response(create_tunnel_ops_type::iterator& create_tunnel_op_it, const tunnel_properties& tunnel_properties, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  tunnel tunnel = nullptr;
  if (!error_code) {
    auto tunnel_id = tunnel_properties.m_id.get();
    auto endpoint  = (struct endpoint){.m_id_name = tunnel_properties.m_id.get(), .m_server_address = m_server_address};
    tunnel.m_impl  = std::allocate_shared<tunnel::impl>(core::allocator::wrapper<impl>(m_allocator), tunnel_id, endpoint, tunnel_properties, shared_from_this());
    m_tunnels.insert(std::make_pair(tunnel_id, std::allocate_shared<tunnel_type>(core::allocator::wrapper<impl>(m_allocator))));
  }
  rstream::core::invoke_completion_handler(m_executor, std::move(create_tunnel_op_it->second->m_handler), error_code, std::move(tunnel));
  create_tunnel_op_it = m_create_tunnel_ops.erase(create_tunnel_op_it);
}

void client::impl::do_close_tunnel(const tunnel_type::id& tunnel_id)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  protobuf::Message message;
  protobuf::CloseTunnelReq payload;
  payload.set_tunnel_id(tunnel_id);
  message.mutable_close_tunnel_req()->CopyFrom(payload);
  do_send_message(message);
}

void client::impl::on_tunnel_close(tunnels_type::iterator& tunnel_it)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  for (auto it = tunnel_it->second->m_streams.begin(); it != tunnel_it->second->m_streams.end(); ++it) {
    if (it->second.m_ptr) {
      boost::system::error_code error_code;
      it->second.m_ptr->m_peer.close(error_code);
    }
  }
  tunnel_it->second->m_streams.clear();
  if (tunnel_it->second->m_accept_tunnel_op) {
    if (tunnel_it->second->m_accept_tunnel_op->m_busy) {
      boost::system::error_code error_code;
      tunnel_it->second->m_accept_tunnel_op->m_peer.close(error_code);
    }
    rstream::core::invoke_completion_handler(m_executor, std::move(tunnel_it->second->m_accept_tunnel_op->m_handler), error::code::tunnel_not_found);
  }
  tunnel_it = m_tunnels.erase(tunnel_it);
}

void client::impl::on_proxy_request(tunnels_type::iterator& tunnel_it, const stream_type::id& stream_id, const stream_type::context& context)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  boost::system::error_code error_code;
  if (m_config.m_max_ongoing_streams > 0 && tunnel_it->second->m_streams.size() >= m_config.m_max_ongoing_streams) {
    error_code = error::code::operation_aborted;
#ifdef DEBUG_BUILD
    m_logger->warn("maximum number of ongoing streams reached [tunnel_id={}, stream_id={}]", tunnel_it->first, stream_id);
#endif
  }
  else if (tunnel_it->second->m_streams.find(stream_id) != tunnel_it->second->m_streams.end()) {
    error_code = error::code::operation_in_progress;
#ifdef DEBUG_BUILD
    m_logger->warn("stream already exists [tunnel_id={}, stream_id={}]", tunnel_it->first, stream_id);
#endif
  }
  else {
    stream_type::ptr stream_ptr = nullptr;
    if (m_config.m_async_stream_operation) {
      stream_ptr = std::allocate_shared<stream_type>(core::allocator::wrapper<impl>(m_allocator), m_executor, m_allocator);
    }
    auto stream_it = tunnel_it->second->m_streams.insert(std::make_pair(stream_id, (stream_type::container){.m_ptr = stream_ptr, .m_context = context})).first;
    do_connect_stream(tunnel_it, stream_it);
  }
  if (error_code) {
    do_send_proxy_response(stream_id, error_code);
  }
}

void client::impl::do_send_proxy_response(const stream_type::id& stream_id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  protobuf::Message message;
  protobuf::ProxyConnRsp payload;
  payload.set_stream_id(stream_id);
  if (error_code) {
    protobuf::Error error;
    error.set_code(static_cast<protobuf::ErrorCode>(error_code.value()));
    google::protobuf::StringValue str;
    str.set_value(error_code.message());
    error.mutable_message()->CopyFrom(str);
    payload.mutable_error()->CopyFrom(error);
  }
  message.mutable_proxy_conn_rsp()->CopyFrom(payload);
  do_send_message(message);
}

void client::impl::do_connect_stream(tunnels_type::iterator& tunnel_it, streams_type::iterator& stream_it)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  socket* peer = nullptr;
  if (stream_it->second.m_ptr) {
    peer = &stream_it->second.m_ptr->m_peer;
  }
  else if (tunnel_it->second->m_accept_tunnel_op && !tunnel_it->second->m_accept_tunnel_op->m_busy) {
    tunnel_it->second->m_accept_tunnel_op->m_busy = true;
    peer                                          = &tunnel_it->second->m_accept_tunnel_op->m_peer;
  }
  if (peer) {
    const auto& context     = stream_it->second.m_context;
    const auto address      = context.m_proxy_endpoint ? context.m_proxy_endpoint.get() : m_server_address;
    endpoint endpoint       = {.m_id_name = stream_it->first, .m_server_address = address, .m_secret = context.m_secret, .m_source_ip = context.m_source_ip};
    auto completion_handler = std::bind(&impl::on_connect_stream, shared_from_this(), tunnel_it->first, stream_it->first, std::placeholders::_1);
    boost::system::error_code error_code;
    if (!error_code) {
      struct settings_socket settings_socket;
      settings_socket.m_config = m_config;
      peer->settings(settings_socket, error_code);
    }
    if (!error_code) {
#ifdef DEBUG_BUILD
      m_logger->trace("connecting to peer [tunnel_id={}, stream_id={}]", tunnel_it->first, stream_it->first);
#endif
      peer->async_connect_internal(socket::type::server, endpoint, boost::asio::bind_executor(m_strand, completion_handler));
    }
    else {
      on_connect_stream(tunnel_it->first, stream_it->first, error_code);
    }
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->trace("peer connection delayed until an accept operation is available [tunnel_id={}, stream_id={}]", tunnel_it->first, stream_it->first);
#endif
  }
}

void client::impl::on_connect_stream(const tunnel_type::id& tunnel_id, const stream_type::id& stream_id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected) {
    return;
  }
  tunnels_type::value_type::second_type tunnel;
  {
    auto it = m_tunnels.find(tunnel_id);
    if (it == m_tunnels.end()) {
      do_send_proxy_response(stream_id, error::code::tunnel_not_found);
      return;
    }
    else {
      tunnel = it->second;
    }
  }
  auto it = tunnel->m_streams.find(stream_id);
  if (it == tunnel->m_streams.end()) {
    do_send_proxy_response(stream_id, error::code::invalid_stream);
    return;
  }
  if (tunnel->m_accept_tunnel_op) {
    tunnel->m_accept_tunnel_op->m_busy = false;
  }
  if (error_code) {
#ifdef DEBUG_BUILD
    m_logger->trace("failed to connect to peer [tunnel_id={}, stream_id={}, error_code: {}]", tunnel_id, stream_id, error_code.message());
#endif
    it = tunnel->m_streams.erase(it);
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->trace("connected to peer [tunnel_id={}, stream_id={}]", tunnel_id, stream_id);
#endif
    if (tunnel->m_accept_tunnel_op) {
      if (it->second.m_ptr) {
        tunnel->m_accept_tunnel_op->m_peer = std::move(it->second.m_ptr->m_peer);  // TODO : Properly rebind executor
      }
      const auto address                     = it->second.m_context.m_proxy_endpoint ? it->second.m_context.m_proxy_endpoint.get() : m_server_address;
      tunnel->m_accept_tunnel_op->m_endpoint = (endpoint){.m_id_name = it->first, .m_server_address = address, .m_secret = boost::none, .m_source_ip = it->second.m_context.m_source_ip};
      rstream::core::invoke_completion_handler(m_executor, std::move(tunnel->m_accept_tunnel_op->m_handler), boost::system::error_code());
      it                         = tunnel->m_streams.erase(it);
      tunnel->m_accept_tunnel_op = nullptr;
#ifdef DEBUG_BUILD
      m_logger->trace("accept operation completed [tunnel_id={}, stream_id={}]", tunnel_id, stream_id);
#endif
    }
    else if (it->second.m_ptr) {
#ifdef DEBUG_BUILD
      m_logger->trace("completion handler invocation delayed until an accept operation is available [tunnel_id={}, stream_id={}]", tunnel_id, stream_id);
#endif
      it->second.m_ptr->m_connected = true;
    }
  }
  do_send_proxy_response(stream_id, error_code);
}

void client::impl::do_read_incoming_message()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
  m_buffer.reset_size();
  auto completion_handler = std::bind(&impl::on_read_incoming_data, shared_from_this(), std::placeholders::_1);
  m_payloader.async_recv(m_buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_read_incoming_data(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    protobuf::Message message;
    if (message.ParseFromArray(m_buffer.map().get_const_data(), m_buffer.get_size())) {
      on_read_incoming_message(message);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

void client::impl::on_read_incoming_message(const protobuf::Message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("received message from peer\n{}", core::helpers::to_json_string(message));
#endif
  boost::system::error_code error_code;
  if (!is_message_expected(m_state, message)) {
    error_code = error::code::protocol_error;
  }
  else {
    using payload_type = protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kOpenControlChannelRsp) {
      auto payload = message.open_control_channel_rsp();
      if (payload.has_ok()) {
        const auto& ok = payload.ok();
        if (!ok.client_id().empty()) {
          if (ok.has_server_details()) {
            const auto& details = ok.server_details();
            if (details.has_plan()) {
              m_status.m_plan = details.plan().value();
            }
            if (details.has_provider()) {
              m_status.m_provider = details.provider().value();
            }
            if (details.has_region()) {
              m_status.m_region = details.region().value();
            }
            if (details.has_update()) {
              m_status.m_update = details.update().value();
            }
          }
          on_open();
        }
        else {
#ifdef DEBUG_BUILD
          m_logger->trace("received open response with no client ID\n{}", core::helpers::to_json_string(payload));
#endif
          error_code = error::code::protocol_error;
        }
      }
      else if (payload.has_error()) {
        error_code = error::make_error_code(static_cast<int>(payload.error().code()));
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->trace("received open response with no ok or error\n{}", core::helpers::to_json_string(payload));
#endif
        error_code = error::code::protocol_error;
      }
    }
    else if (message_type == payload_type::kOpenTunnelRsp) {
      create_tunnel_ops_type::iterator it;
      tunnel_properties tunnel_properties;
      boost::system::error_code error;
      {
        auto payload = message.open_tunnel_rsp();
        it           = m_create_tunnel_ops.find(payload.request_id());
        if (it == m_create_tunnel_ops.end()) {
#ifdef DEBUG_BUILD
          m_logger->trace("received tunnel response with no matching request\n{}", core::helpers::to_json_string(payload));
#endif
          error_code = error::code::protocol_error;
        }
        else {
          if (payload.has_tunnel_properties()) {
            detail::convert(tunnel_properties, payload.tunnel_properties());
            if (!tunnel_properties.m_id) {
#ifdef DEBUG_BUILD
              m_logger->trace("received tunnel response with no tunnel ID\n{}", core::helpers::to_json_string(payload));
#endif
              error_code = error::code::protocol_error;
            }
            if (!error_code && !error && tunnel_properties.m_type && tunnel_properties.m_type.get() != "bytestream") {
              error = error::make_error_code(error::code::invalid_configuration);
            }
          }
          else if (payload.has_error()) {
            error = error::make_error_code(static_cast<int>(payload.error().code()));
          }
          else {
#ifdef DEBUG_BUILD
            m_logger->trace("received tunnel response with no properties or error\n{}", core::helpers::to_json_string(payload));
#endif
            error_code = error::code::protocol_error;
          }
        }
      }
      if (!error_code) {
        on_tunnel_response(it, tunnel_properties, error);
      }
    }
    else if (message_type == payload_type::kCloseTunnelRsp) {
      tunnels_type::iterator it;
      {
        auto payload = message.close_tunnel_rsp();
        it           = m_tunnels.find(payload.tunnel_id());
        if (it == m_tunnels.end()) {
#ifdef DEBUG_BUILD
          m_logger->trace("received tunnel close response with no matching tunnel\n{}", core::helpers::to_json_string(payload));
#endif
          error_code = error::code::protocol_error;
        }
      }
      if (!error_code) {
        on_tunnel_close(it);
      }
    }
    else if (message_type == payload_type::kProxyConnReq) {
      tunnels_type::iterator it;
      stream_type::id stream_id;
      stream_type::context context;
      boost::system::error_code request_error;
      {
        auto payload = message.proxy_conn_req();
        stream_id    = payload.stream_id();
        it           = m_tunnels.find(payload.tunnel_id());
        if (it == m_tunnels.end()) {
#ifdef DEBUG_BUILD
          m_logger->trace("received proxy connection request with no matching tunnel\n{}", core::helpers::to_json_string(payload));
#endif
          error_code = error::code::protocol_error;
        }
        if (!error_code) {
          if (payload.has_secret()) {
            context.m_secret = payload.secret().value();
          }
        }
        if (!error_code && payload.has_proxy_endpoint()) {
          if (!context.m_secret
              || boost::algorithm::trim_copy(context.m_secret.value()).empty()
              || boost::algorithm::trim_copy(payload.proxy_endpoint().value()).empty()) {
            request_error = error::code::protocol_error;
          }
          else {
            auto endpoint_result = make_redirected_server_address(m_server_address, payload.proxy_endpoint().value());
            if (endpoint_result) {
              context.m_proxy_endpoint = endpoint_result.value();
            }
            else {
              request_error = endpoint_result.error();
            }
          }
        }
        if (!error_code) {
          if (payload.has_source_ip()) {
            boost::asio::ip::address source_ip;
            try {
              detail::convert(source_ip, payload.source_ip());
              context.m_source_ip = source_ip;
            }
            catch (...) {
#ifdef DEBUG_BUILD
              m_logger->trace("failed to convert source IP address: {}", core::throwable::message(std::current_exception()));
#endif
            }
          }
        }
      }
      if (!error_code) {
        if (request_error) {
          do_send_proxy_response(stream_id, request_error);
        }
        else {
          on_proxy_request(it, stream_id, context);
        }
      }
    }
    else if (message_type == payload_type::kCloseControlChannelRsp) {
      boost::system::error_code error;
      if (m_state != state::closing) {
        error = error::code::server_error;
      }
      on_close(error);
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_incoming_message();
  }
}

void client::impl::do_send_message(const protobuf::Message& message, const on_send_message_cb& cb)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("sending message to peer\n{}", core::helpers::to_json_string(message));
#endif
  std::size_t buffer_size = message.ByteSizeLong();
  auto buffer             = core::make_buffer_allocated(buffer_size, m_allocator);
  message.SerializeToArray(buffer.map().get_data(), buffer_size);
  auto completion_handler = std::bind(&impl::on_send_message, shared_from_this(), std::placeholders::_1, cb);
  m_queue.async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_send_message(const boost::system::error_code& error_code, const on_send_message_cb& cb)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else if (cb) {
    cb();
  }
}

void client::impl::close_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
  else if (m_state == state::connecting) {
    on_close(error_code);
  }
  else if (m_state != state::closing) {
#ifdef DEBUG_BUILD
    m_logger->trace("closing client...");
#endif
    set_state(state::closing);
    arm_state_timer(m_config.m_connection_timeout_ms);
    if (error_code && !m_error_code) {
      m_error_code = error_code;
    }
    protobuf::Message message;
    protobuf::CloseControlChannelReq payload;
    message.mutable_close_control_channel_req()->CopyFrom(payload);
    do_send_message(message);
  }
}

void client::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void client::impl::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null) {
    return;
  }
  boost::system::error_code cause;
  if (m_error_code) {
    cause = m_error_code;
  }
  else if (error_code) {
    cause = error_code;
  }
  for (auto it = m_create_tunnel_ops.begin(); it != m_create_tunnel_ops.end();) {
    on_tunnel_response(it, tunnel_properties(), cause ? cause : error::code::operation_aborted);
  }
  for (auto it = m_tunnels.begin(); it != m_tunnels.end();) {
    on_tunnel_close(it);
  }
  if (m_state == state::connected || m_state == state::closing) {
    if (m_control_callbacks.m_on_disconnection_cb) {
      m_control_callbacks.m_on_disconnection_cb(cause);
    }
  }
  else if (m_state == state::connecting) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause ? cause : error::code::operation_aborted);
  }
  {
    boost::system::error_code tmp;
    m_resolver.cancel();
    m_socket.close(tmp);
    m_queue.cancel();
  }
  m_buffer  = core::buffer(m_allocator);
  m_handler = nullptr;
  m_create_tunnel_ops.clear();
  m_tunnels.clear();
  m_error_code     = boost::system::error_code();
  m_client_details = {};
  m_status         = {};
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    set_state(state::null);
    m_is_state_non_null = false;
    m_server_address    = io::address();
    m_control_callbacks = {};
  }
#ifdef DEBUG_BUILD
  m_logger->trace("client closed");
#endif
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
  auto timeout_ms = m_config.m_heartbeat_interval_ms;
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
      protobuf::Message message;
      message.mutable_heartbeat();
      ptr->do_send_message(message, std::bind(&impl::do_send_heartbeat, ptr));
    }
    task_ptr->clean();
  };
  task_ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
  auto completion_handler = boost::asio::bind_executor(ptr->m_strand, on_timer_cb);
  task_ptr->m_timer.async_wait(completion_handler);
}

std::string client::impl::generate_request_id()
{
  return rstream::core::object_id();
}

bool client::impl::is_message_expected(state state, const protobuf::Message& message)
{
  using payload_type                                          = protobuf::Message::PayloadCase;
  using compatibility_matrix_type                             = std::map<impl::state, std::set<payload_type>>;
  static const compatibility_matrix_type compatibility_matrix = {
      {state::connecting, {payload_type::kOpenControlChannelRsp}},
      {state::connected, {payload_type::kOpenTunnelRsp, payload_type::kCloseTunnelRsp, payload_type::kProxyConnReq, payload_type::kCloseControlChannelRsp, payload_type::kHeartbeat, payload_type::kServerMessage}},
      {state::closing, {payload_type::kOpenTunnelRsp, payload_type::kCloseTunnelRsp, payload_type::kProxyConnReq, payload_type::kCloseControlChannelRsp, payload_type::kHeartbeat, payload_type::kServerMessage}},
  };
  const auto& set = compatibility_matrix.find(state)->second;
  return set.find(message.payload_case()) != set.end();
}

}  // namespace io_rstrm
}  // namespace rstream
