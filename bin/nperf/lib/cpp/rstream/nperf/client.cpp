// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/deadline_timer.hpp>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#include <boost/asio/read.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/buffers_adaptor.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/optional.hpp>
#include <boost/signals2.hpp>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/random.hpp>
#include <rstream/io/error.hpp>
#include <rstream/io/payloader.hpp>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/async_connect.hpp>
#include <rstream/io/detail/stream/websocket.hpp>
#include <rstream/io/stream.hpp>
#endif

#include <rstream/nperf/protobuf/messages.pb.h>

#include "detail/convert.hpp"
#include "error.hpp"
#include "nperf.hpp"

namespace rstream {
namespace nperf {

class RSTREAM_GNUC_INTERNAL client::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_client& settings);

  virtual ~impl() = default;

  void async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler);

  void cancel();

 private:
  class base;

  using cancel_signal_type = boost::signals2::signal_type<void(), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void async_run_internal(options options, const callbacks& callbacks, async_run_completion_handler&& handler);

  void cancel_internal();

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const config m_config;

  const settings_client m_settings;

  cancel_signal_type m_cancel_signal;
};

class RSTREAM_GNUC_INTERNAL client::impl::base : public std::enable_shared_from_this<base> {
 public:
  base(const executor_type& executor, const config& config, const settings_client& settings);

  virtual ~base() = default;

  void async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler);

  void cancel();

 private:
  class session;

#ifdef RSTREAM_WITH_IO_STREAMS
  using protocol_type = rstream::io::stream;
#else
  using protocol_type = boost::asio::ip::tcp;
#endif

  using resolver_type = protocol_type::resolver;

  using session_id_type = std::size_t;

  using session_ptr_type = std::shared_ptr<session>;

  enum class session_state {
    null    = 0,
    opening = 1,
    opened  = 2,
    running = 3,
    closed  = 4
  };

  using sessions_type = std::map<session_id_type, std::pair<session_ptr_type, session_state>>;

  enum class state {
    null          = 0,
    connecting    = 1,
    connected     = 2,
    disconnecting = 3,
    disconnected  = 4
  };

  enum class metrics_type {
    connection,
    handshake,
    others,
  };

  using state_changed_signal_type = boost::signals2::signal_type<void(state), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void set_state(state state);

  void set_session_state(session_id_type session_id, session_state state, const boost::system::error_code& cause = boost::system::error_code());

  void arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code = boost::system::error_code());

  void arm_metrics_timer();

  void async_run_internal(options options, const callbacks& callbacks, async_run_completion_handler&& handler);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results);

  void do_open(const resolver_type::results_type& results);

  void on_open(session_id_type session_id, const boost::system::error_code& error_code);

  void on_open();

  void do_run();

  void on_connection_handshake_us_cb(session_id_type session_id, std::uint64_t connection_us, std::uint64_t handshake_us);

  void on_ping_us(session_id_type session_id, std::uint64_t ping_us);

  void on_bytes_transferred(session_id_type session_id, std::uint64_t bytes_transferred);

  void on_run(session_id_type session_id, const boost::system::error_code& error_code);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  metrics get_metrics(metrics_type type = metrics_type::others) const;

  void transpond() const;

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  const config m_config;

  const settings_client m_settings;

  rstream::core::logger m_logger;

  state m_state;

  options m_options;

  callbacks m_callbacks;

  async_run_completion_handler m_handler;

  resolver_type m_resolver;

  boost::system::error_code m_error_code;

  state_changed_signal_type m_state_changed_signal;

  sessions_type m_sessions;

  struct {
    std::vector<std::uint64_t> m_connection_sample_us;
    std::vector<std::uint64_t> m_handshake_sample_us;
    std::vector<std::uint64_t> m_ping_sample_us;
    std::uint64_t m_measured_bytes;
    timestamp m_start_time_ms;
  } m_metrics;
};

class RSTREAM_GNUC_INTERNAL client::impl::base::session : public std::enable_shared_from_this<session> {
 public:
  using ptr = std::shared_ptr<session>;

  using on_connection_handshake_us_cb = std::function<void(std::uint64_t, std::uint64_t)>;

  using on_ping_us_cb = std::function<void(std::uint64_t)>;

  using on_bytes_transferred_cb = std::function<void(std::uint64_t)>;

  struct open_callbacks {
    on_connection_handshake_us_cb m_on_connection_handshake_us_cb;
  };

  struct run_callbacks {
    on_ping_us_cb m_on_ping_cb;
    on_bytes_transferred_cb m_on_bytes_transferred_cb;
  };

  session(const executor_type& executor, protocol protocol, options options, const settings_client& settings, const session_id_type& session_id);

  using async_open_completion_handler = rstream::core::completion_handler<void(const boost::system::error_code)>;

  void async_open(const open_callbacks& open_callbacks, const io::address& address, const resolver_type::results_type& results, async_open_completion_handler&& handler);

  using async_run_completion_handler = async_open_completion_handler;

  void async_run(const run_callbacks& run_callbacks, async_run_completion_handler&& handler);

  void cancel(const boost::system::error_code& error_code);

 private:
  using socket_type = protocol_type::socket;

  using websocket_type = std::shared_ptr<boost::beast::websocket::stream<socket_type&, false>>;

  using payloader_type = std::shared_ptr<rstream::io::payloader<socket_type&>>;

  enum class loop {
    null,
    read_data,
    read_dummy,
    write_data,
    send_ping,
    recv_ping,
  };

  enum class state {
    null    = 0,
    opening = 1,
    opened  = 2,
    running = 3,
    closing = 4,
    closed  = 5
  };

  using on_activity_signal_type = boost::signals2::signal_type<void(boost::beast::websocket::frame_type, const boost::beast::string_view&), boost::signals2::keywords::mutex_type<boost::signals2::dummy_mutex>>::type;

  void async_open_internal(const open_callbacks& open_callbacks, const io::address& address, const resolver_type::results_type& results, async_open_completion_handler&& handler);

  void do_connect(const io::address& address, const resolver_type::results_type& results);

  void on_connect(const boost::system::error_code& error_code, const io::address& address, const resolver_type::results_type::endpoint_type& endpoint);

  void do_handshake_websocket(const io::address& address, const resolver_type::results_type::endpoint_type& endpoint);

  void do_handshake_protobuf();

  void do_read_incoming_protobuf_message(enum loop loop);

  void on_read_incoming_protobuf_data(const boost::system::error_code& error_code, enum loop loop);

  void on_read_incoming_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop);

  void do_send_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop);

  void on_send_protobuf_message(const boost::system::error_code& error_code, enum loop loop);

  void on_handshake(const boost::system::error_code& error_code);

  void on_open();

  void async_run_internal(const run_callbacks& run_callbacks, async_run_completion_handler&& handler);

  void run_loop();

  void ping_loop();

  void download_loop();

  void upload_loop();

  void do_read_write(enum loop loop);

  void on_read_write(const boost::system::error_code& error_code, std::size_t bytes_transferred, enum loop loop);

  void on_control_callback(boost::beast::websocket::frame_type kind, const boost::beast::string_view& payload);

  void on_pong(const std::string& data);

  void cancel_internal(const boost::system::error_code& error_code);

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void on_close(const boost::system::error_code& error_code);

  executor_type m_executor;

  boost::asio::strand<executor_type> m_strand;

  socket_type m_socket;

  websocket_type m_websocket;

  payloader_type m_payloader;

  const session_id_type m_session_id;

  rstream::core::logger m_logger;

  state m_state;

  options m_options;

  const settings_client m_settings;

  open_callbacks m_open_callbacks;

  run_callbacks m_run_callbacks;

  async_open_completion_handler m_handler_open;

  async_run_completion_handler m_handler_run;

  boost::system::error_code m_error_code;

  rstream::core::memory m_buffer;

  boost::beast::buffers_adaptor<boost::asio::mutable_buffer> m_http_buffers_adaptor;

  std::pair<timestamp, timestamp> m_connection;

  std::pair<timestamp, timestamp> m_handshake;

  struct {
    bool m_active;
    bool m_busy;
    timestamp m_timestamp;
    boost::beast::websocket::ping_data m_data;
  } m_ping;
};

static sample compute_sample(const std::vector<std::uint64_t>& sample, sample::type type);

client::client(const executor_type& executor, const config& config, const settings_client& settings)
    : io::io_object(executor)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

client::~client()
{
  m_impl->cancel();
}

void client::async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler)
{
  m_impl->async_run(options, std::forward<decltype(callbacks)>(callbacks), std::forward<decltype(handler)>(handler));
}

void client::cancel()
{
  m_impl->cancel();
}

client::impl::impl(const executor_type& executor, const config& config, const settings_client& settings)
    : m_executor(executor),
      m_strand(executor),
      m_config(config),
      m_settings(settings)
{
}

void client::impl::async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), options, callbacks, std::move(handler)));
}

void client::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::cancel_internal, shared_from_this()));
}

void client::impl::async_run_internal(options options, const callbacks& callbacks, async_run_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (options == 0) {
    if (handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_argument);
    }
    return;
  }
  struct task : std::enable_shared_from_this<task> {
    task(nperf::options options, const struct callbacks& callbacks, async_run_completion_handler&& handler)
        : m_options(options),
          m_execution_count(0),
          m_cancelled(false),
          m_complete(false),
          m_callbacks(callbacks),
          m_handler(std::move(handler))
    {
      reset();
    }
    void run(bool error_occured)
    {
#ifdef DEBUG_BUILD
      assert(m_parent->m_strand.running_in_this_thread());
#endif
      if (m_options_tmp == 0) {
        if (!m_timer && !error_occured) {
          ++m_execution_count;
        }
        if (m_parent->m_settings.m_execution_count != 0 && m_execution_count >= m_parent->m_settings.m_execution_count) {
          return on_complete(boost::system::error_code());
        }
        else {
          if (m_parent->m_settings.m_period_ms != 0) {
            if (!m_timer) {
              m_timer = std::make_shared<boost::asio::deadline_timer>(m_parent->m_executor);
              m_timer->expires_at(m_start + boost::posix_time::milliseconds(m_parent->m_settings.m_period_ms));
              auto completion_handler = std::bind(&task::on_run, shared_from_this(), std::placeholders::_1);
              m_timer->async_wait(boost::asio::bind_executor(m_parent->m_strand, completion_handler));
              return;
            }
            m_timer = nullptr;
          }
          reset();
        }
      }
      if (m_options_tmp == m_options) {
        m_start = boost::asio::deadline_timer::traits_type::now();
      }
      auto completion_handler = std::bind(&task::on_run, shared_from_this(), std::placeholders::_1);
      auto option             = (1U << m_i);
      if (m_options_tmp & option) {
        m_base = std::make_shared<base>(m_parent->m_executor, m_parent->m_config, m_parent->m_settings);
        m_base->async_run(option, m_callbacks, boost::asio::bind_executor(m_parent->m_strand, completion_handler));
      }
      else {
        completion_handler(boost::system::error_code());
      }
    }
    void on_run(const boost::system::error_code& error_code)
    {
#ifdef DEBUG_BUILD
      assert(m_parent->m_strand.running_in_this_thread());
#endif
      auto cause = error_code;
      if (cause && m_timer && m_cancelled) {
        cause = error::code::operation_aborted;
      }
      else if (cause && m_parent->m_settings.m_retry) {
        auto option = (1U << m_i);
        if (m_options & option) {
          auto now               = timestamp::clock::now();
          struct metrics metrics = {
              .m_final     = true,
              .m_options   = option,
              .m_timestamp = now,
              .m_data      = cause,
          };
          if (m_callbacks.m_on_metrics_cb) {
            m_callbacks.m_on_metrics_cb(metrics);
          }
        }
        cause = boost::system::error_code();
      }
      if (cause || m_cancelled) {
        on_complete(cause);
      }
      else {
        if (!m_timer) {
          m_base = nullptr;
          if (error_code) {
            m_options_tmp = 0;
          }
          else {
            auto option = (1U << m_i);
            m_options_tmp &= ~option;
            ++m_i;
          }
        }
        run(error_code ? true : false);
      }
    }
    void reset()
    {
      m_options_tmp = m_options;
      m_i           = 1;
    }
    void cancel()
    {
#ifdef DEBUG_BUILD
      assert(m_parent->m_strand.running_in_this_thread());
#endif
      m_cancelled = true;
      if (m_base) {
        m_base->cancel();
      }
      if (m_timer) {
        m_timer->cancel();
      }
    }
    void on_complete(const boost::system::error_code& error_code)
    {
#ifdef DEBUG_BUILD
      assert(m_parent->m_strand.running_in_this_thread());
#endif
      if (m_complete) {
        return;
      }
      m_complete     = true;
      m_base         = nullptr;
      m_timer        = nullptr;
      m_signal_state = boost::signals2::scoped_connection();
      if (m_handler) {
        rstream::core::invoke_completion_handler(m_parent->m_strand, std::move(m_handler), error_code);
      }
      m_handler = nullptr;
    }
    const nperf::options m_options;
    std::uint32_t m_execution_count;
    bool m_cancelled;
    bool m_complete;
    nperf::options m_options_tmp;
    struct callbacks m_callbacks;
    async_run_completion_handler m_handler;
    unsigned int m_i;
    boost::asio::deadline_timer::traits_type::time_type m_start;
    std::shared_ptr<base> m_base;
    boost::signals2::scoped_connection m_signal_state;
    std::shared_ptr<boost::asio::deadline_timer> m_timer;
    std::shared_ptr<client::impl> m_parent;
  };
  auto task            = std::make_shared<struct task>(options, callbacks, std::move(handler));
  task->m_signal_state = m_cancel_signal.connect(std::bind(&task::cancel, task));
  task->m_parent       = shared_from_this();
  task->run(false);
}

void client::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_cancel_signal();
}

client::impl::base::base(const executor_type& executor, const config& config, const settings_client& settings)
    : m_executor(executor),
      m_strand(executor),
      m_config(config),
      m_settings(settings),
      m_logger({"rstream", "nperf", "client", fmt::format("#{}", fmt::ptr(this))}),
      m_state(state::null),
      m_options(0),
      m_resolver(executor)
{
}

void client::impl::base::async_run(options options, const callbacks& callbacks, async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&base::async_run_internal, shared_from_this(), options, callbacks, std::move(handler)));
}

void client::impl::base::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&base::cancel_internal, shared_from_this(), error::code::operation_aborted));
}

void client::impl::base::set_state(state state)
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
  m_logger->debug("client state changed to '{}'", str.str());
}

void client::impl::base::set_session_state(session_id_type session_id, session_state state, const boost::system::error_code& cause)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  {
    auto it           = m_sessions.find(session_id);
    it->second.second = state;
  }
  std::stringstream str;
  switch (state) {
    case session_state::opening:
      str << "opening";
      break;
    case session_state::opened:
      str << "opened";
      break;
    case session_state::running:
      str << "running";
      break;
    case session_state::closed:
      str << "closed";
      break;
    default:
      break;
  }
  m_logger->debug("session #{} state changed to '{}'", session_id, str.str());
  enum class sessions_state {
    all_closed,
    all_opened,
    other
  };
  auto get_sessions_state = [this]() -> sessions_state {
    bool all_closed = true;
    bool all_opened = true;
    for (const auto& session : m_sessions) {
      if (session.second.second != session_state::closed) {
        all_closed = false;
      }
      if (session.second.second != session_state::opened) {
        all_opened = false;
      }
      if (!all_closed && !all_opened) {
        break;
      }
    }
    if (all_closed) {
      return sessions_state::all_closed;
    }
    else if (all_opened) {
      return sessions_state::all_opened;
    }
    else {
      return sessions_state::other;
    }
  };
  auto sessions_state = get_sessions_state();
  if (sessions_state == sessions_state::all_closed) {
    return on_close(cause);
  }
  if ((m_state == state::connecting || m_state == state::connected) && state == session_state::closed) {
    return cancel_internal(cause);  // unexpected closed
  }
  if (m_state == state::connecting && sessions_state == sessions_state::all_opened) {
    return on_open();
  }
}

void client::impl::base::arm_state_timer(unsigned int timeout_ms, const boost::system::error_code& error_code)
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

void client::impl::base::arm_metrics_timer()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_settings.m_period_metrics_ms == 0) {
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
    std::function<void(const boost::system::error_code&)> m_on_timer_cb;
  };
  auto ptr         = shared_from_this();
  auto task_ptr    = std::make_shared<struct task>(m_executor);
  auto on_state_cb = [task_ptr, ptr](state) {
    if (task_ptr->m_complete) {
      return;
    }
    task_ptr->m_complete = true;
    task_ptr->m_timer.cancel();
    task_ptr->m_on_timer_cb  = nullptr;
    task_ptr->m_signal_state = boost::signals2::scoped_connection();
  };
  task_ptr->m_signal_state = m_state_changed_signal.connect(on_state_cb);
  auto arm_timer           = [task_ptr, ptr, timeout_ms = m_settings.m_period_metrics_ms]() {
    task_ptr->m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
    auto completion_handler = boost::asio::bind_executor(ptr->m_strand, task_ptr->m_on_timer_cb);
    task_ptr->m_timer.async_wait(completion_handler);
  };
  auto on_timer_cb = [task_ptr, ptr, arm_timer](const boost::system::error_code& error_code) {
    if (task_ptr->m_complete || error_code) {
      return;
    }
    ptr->transpond();
    arm_timer();
  };
  task_ptr->m_on_timer_cb = on_timer_cb;
  arm_timer();
}

void client::impl::base::async_run_internal(options options, const callbacks& callbacks, async_run_completion_handler&& handler)
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
  m_options   = options;
  m_callbacks = callbacks;
  m_handler.swap(handler);
  set_state(state::connecting);
  arm_state_timer(m_settings.m_common.m_timeouts_open_close_ms, error::code::operation_timeout);
  do_resolve_host();
}

void client::impl::base::do_resolve_host()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto completion_handler = std::bind(&base::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
#ifdef RSTREAM_WITH_IO_STREAMS
  m_resolver.async_resolve(m_config.m_address.m_url, boost::asio::bind_executor(m_strand, completion_handler));
#else
  m_resolver.async_resolve(m_config.m_address.host(), m_config.m_address.port(), boost::asio::bind_executor(m_strand, completion_handler));
#endif
}

void client::impl::base::on_do_resolve_host(const boost::system::error_code& error_code, const resolver_type::results_type& results)
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
    do_open(results);
  }
}

void client::impl::base::do_open(const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  for (std::size_t i = 0; i < m_settings.m_sessions; ++i) {
    m_sessions.insert(std::make_pair(i, std::make_pair(std::make_shared<session>(m_executor, m_settings.m_common.m_protocol, m_options, m_settings, i), session_state::null)));
  }
  auto ptr = shared_from_this();
  for (auto& session : m_sessions) {
    session::open_callbacks open_callbacks = {
        .m_on_connection_handshake_us_cb = rstream::core::wrap_function<void(std::uint64_t, std::uint64_t)>(m_strand, std::bind(&base::on_connection_handshake_us_cb, ptr, session.first, std::placeholders::_1, std::placeholders::_2)),
    };
    set_session_state(session.first, session_state::opening);
    auto completion_handler = std::bind((void(base::*)(session_id_type, const boost::system::error_code&)) & base::on_open, shared_from_this(), session.first, std::placeholders::_1);
    session.second.first->async_open(open_callbacks, m_config.m_address, results, boost::asio::bind_executor(m_strand, completion_handler));
  }
}

void client::impl::base::on_open(session_id_type session_id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting && m_state != state::disconnecting) {
    return;
  }
  set_session_state(session_id, (error_code || m_state == state::disconnecting) ? session_state::closed : session_state::opened, error_code);
}

void client::impl::base::on_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  set_state(state::connected);
  if (m_callbacks.m_on_metrics_cb) {
    m_callbacks.m_on_metrics_cb(get_metrics(metrics_type::connection));
    m_callbacks.m_on_metrics_cb(get_metrics(metrics_type::handshake));
  }
  do_run();
}

void client::impl::base::do_run()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_metrics.m_measured_bytes = 0;
  if (m_options & option::ping && m_settings.m_max_ping != 0) {
    m_metrics.m_ping_sample_us.reserve(m_settings.m_max_ping);
  }
  m_metrics.m_start_time_ms = timestamp::clock::now();
  arm_state_timer(m_settings.m_common.m_timeouts_max_time_ms);
  arm_metrics_timer();
  auto ptr = shared_from_this();
  for (const auto& session : m_sessions) {
    session::run_callbacks run_callbacks = {
        .m_on_ping_cb              = rstream::core::wrap_function<void(std::uint64_t)>(m_strand, std::bind(&base::on_ping_us, ptr, session.first, std::placeholders::_1)),
        .m_on_bytes_transferred_cb = rstream::core::wrap_function<void(std::uint64_t)>(m_strand, std::bind(&base::on_bytes_transferred, ptr, session.first, std::placeholders::_1)),
    };
    set_session_state(session.first, session_state::running);
    auto completion_handler = std::bind(&base::on_run, ptr, session.first, std::placeholders::_1);
    session.second.first->async_run(run_callbacks, boost::asio::bind_executor(m_strand, completion_handler));
  }
}

void client::impl::base::on_connection_handshake_us_cb(session_id_type session_id, std::uint64_t connection_us, std::uint64_t handshake_us)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)session_id;
  if (m_state != state::connecting) {
    return;
  }
  m_metrics.m_connection_sample_us.push_back(connection_us);
  m_metrics.m_handshake_sample_us.push_back(handshake_us);
}

void client::impl::base::on_ping_us(session_id_type session_id, std::uint64_t ping_us)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)session_id;
  if (m_state != state::connected) {
    return;
  }
  m_metrics.m_ping_sample_us.push_back(ping_us);
  if (m_settings.m_max_ping != 0 && m_metrics.m_ping_sample_us.size() >= m_settings.m_max_ping) {
    cancel_internal(boost::system::error_code());
  }
}

void client::impl::base::on_bytes_transferred(session_id_type session_id, std::uint64_t bytes_transferred)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)session_id;
  if (m_state != state::connected) {
    return;
  }
  m_metrics.m_measured_bytes += bytes_transferred;
  if (m_settings.m_max_data_bytes != 0 && m_metrics.m_measured_bytes >= m_settings.m_max_data_bytes) {
    cancel_internal(boost::system::error_code());
  }
}

void client::impl::base::on_run(session_id_type session_id, const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connected && m_state != state::disconnecting) {
    return;
  }
  set_session_state(session_id, session_state::closed, error_code);
}

void client::impl::base::cancel_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (m_state == state::connecting || m_state == state::connected) {
    do_close(error_code);
  }
  else {
    on_close(error_code);
  }
}

void client::impl::base::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void client::impl::base::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting && m_state != state::connected) {
    return;
  }
  if (m_sessions.empty()) {
    return on_close(error_code);
  }
  set_state(state::disconnecting);
  if (error_code && !m_error_code) {
    m_error_code = error_code;
  }
  arm_state_timer(m_settings.m_common.m_timeouts_open_close_ms);
  for (const auto& session : m_sessions) {
    session.second.first->cancel(error_code);
  }
  for (const auto& session : m_sessions) {
    if (session.second.second == session_state::opened) {
      set_session_state(session.first, session_state::closed);
    }
  }
}

void client::impl::base::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  auto cause = m_error_code ? m_error_code : error_code;
  set_state(state::disconnected);
  for (const auto& session : m_sessions) {
    if (session.second.second != session_state::closed) {
      session.second.first->cancel(boost::system::error_code());
    }
  }
  m_sessions.clear();
  if (!cause && m_callbacks.m_on_metrics_cb) {
    m_callbacks.m_on_metrics_cb(get_metrics());
  }
  m_callbacks = {};
  m_resolver.cancel();
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler = nullptr;
}

metrics client::impl::base::get_metrics(metrics_type type) const
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto now               = timestamp::clock::now();
  struct metrics metrics = {
      .m_final     = true,
      .m_options   = 0,
      .m_timestamp = now,
      .m_data      = {},
  };
  if (type == metrics_type::connection) {
    metrics.m_data = compute_sample(m_metrics.m_connection_sample_us, sample::type::connection);
  }
  else if (type == metrics_type::handshake) {
    metrics.m_data = compute_sample(m_metrics.m_handshake_sample_us, sample::type::handshake);
  }
  else {
    metrics.m_final   = m_state == state::disconnected;
    metrics.m_options = m_options;
    if (m_options & option::ping) {
      metrics.m_data = compute_sample(m_metrics.m_ping_sample_us, sample::type::ping);
    }
    else {
      struct speed speed = {
          .m_measured_bytes  = m_metrics.m_measured_bytes,
          .m_elapsed_time_ms = std::clamp(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - m_metrics.m_start_time_ms).count()), (std::uint32_t)0, m_settings.m_common.m_timeouts_max_time_ms),
          .m_max_time_ms     = m_settings.m_common.m_timeouts_max_time_ms,
      };
      metrics.m_data = speed;
    }
  }
  return metrics;
}

void client::impl::base::transpond() const
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_callbacks.m_on_metrics_cb) {
    m_callbacks.m_on_metrics_cb(get_metrics());
  }
}

client::impl::base::session::session(const executor_type& executor, protocol protocol, options options, const settings_client& settings, const session_id_type& session_id)
    : m_executor(executor),
      m_strand(executor),
      m_socket(executor),
      m_session_id(session_id),
      m_logger({"rstream", "nperf", "client", "session", fmt::format("#{}", session_id)}),
      m_state(state::null),
      m_options(options),
      m_settings(settings),
      m_http_buffers_adaptor(boost::asio::mutable_buffer(nullptr, 0)),
      m_ping({.m_active = false, .m_busy = false})
{
  if (protocol == protocol::websocket) {
    m_websocket = std::make_shared<websocket_type::element_type>(m_socket);
  }
  else if (protocol == protocol::plain) {
    m_payloader = std::make_shared<payloader_type::element_type>(m_socket);
  }
}

void client::impl::base::session::async_open(const open_callbacks& open_callbacks, const io::address& address, const resolver_type::results_type& results, async_open_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_open_internal, shared_from_this(), open_callbacks, address, results, std::move(handler)));
}

void client::impl::base::session::async_run(const run_callbacks& run_callbacks, async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::async_run_internal, shared_from_this(), run_callbacks, std::move(handler)));
}

void client::impl::base::session::cancel(const boost::system::error_code& error_code)
{
  boost::asio::dispatch(m_strand, std::bind_front(&session::cancel_internal, shared_from_this(), error_code));
}

void client::impl::base::session::async_open_internal(const open_callbacks& open_callbacks, const io::address& address, const resolver_type::results_type& results, async_open_completion_handler&& handler)
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
  m_open_callbacks = open_callbacks;
  m_handler_open.swap(handler);
  m_buffer = rstream::core::make_memory_allocated((m_websocket != nullptr && m_options & option::ping) ? 0 : m_settings.m_common.m_buffer_size);
  m_state  = state::opening;
  do_connect(address, results);
}

void client::impl::base::session::do_connect(const io::address& address, const resolver_type::results_type& results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
    return;
  }
  m_connection.first      = timestamp::clock::now();
  auto completion_handler = std::bind(&session::on_connect, shared_from_this(), std::placeholders::_1, address, std::placeholders::_2);
  boost::asio::async_connect(m_socket, results, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::base::session::on_connect(const boost::system::error_code& error_code, const io::address& address, const resolver_type::results_type::endpoint_type& endpoint)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
    return;
  }
  m_connection.second = timestamp::clock::now();
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
  if (error_code) {
    on_error(error_code);
  }
  else {
    if (m_websocket) {
      do_handshake_websocket(address, endpoint);
    }
    else {
      do_handshake_protobuf();
    }
  }
}

void client::impl::base::session::do_handshake_websocket(const io::address& address, const resolver_type::results_type::endpoint_type& endpoint)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
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
    m_websocket->control_callback(rstream::core::wrap_function<void(boost::beast::websocket::frame_type, const boost::beast::string_view&)>(m_strand, completion_handler));
  }
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
  // update the host string. This will provide the value of the
  // host HTTP header during the WebSocket handshake.
  // see : https://tools.ietf.org/html/rfc7230#section-5.4
  auto target       = m_options & option::ping ? "/ping" : m_options & option::download ? "/download"
                                                                                        : "/upload";
  m_handshake.first = timestamp::clock::now();
  // Perform the websocket handshake
  auto completion_handler = std::bind(&session::on_handshake, shared_from_this(), std::placeholders::_1);
  m_websocket->async_handshake(address.host(), target, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
}

void client::impl::base::session::do_handshake_protobuf()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
    return;
  }
  rstream::nperf::protobuf::Message message;
  {
    rstream::nperf::protobuf::Option option;
    detail::convert(option, m_options);
    message.mutable_open()->mutable_config()->set_option(option);
  }
  m_handshake.first = timestamp::clock::now();
  do_read_incoming_protobuf_message(loop::null);
  do_send_protobuf_message(message, loop::null);
}

void client::impl::base::session::do_read_incoming_protobuf_message(enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  m_buffer.reset_size();
  auto completion_handler = std::bind(&session::on_read_incoming_protobuf_data, shared_from_this(), std::placeholders::_1, loop);
  m_payloader->async_recv(m_buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::base::session::on_read_incoming_protobuf_data(const boost::system::error_code& error_code, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    rstream::nperf::protobuf::Message message;
    if (message.ParseFromArray(m_buffer.get_const_data(), m_buffer.get_size())) {
      on_read_incoming_protobuf_message(message, loop);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

void client::impl::base::session::on_read_incoming_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  boost::system::error_code error_code;
#ifdef DEBUG_BUILD
  m_logger->trace("received message from peer\n{}", core::helpers::to_json_string(message));
#endif
  auto is_message_expected = [this](const rstream::nperf::protobuf::Message& message) {
    using payload_type = rstream::nperf::protobuf::Message::PayloadCase;
    std::set<payload_type> expected_messages;
    if (m_state == state::opening) {
      expected_messages.insert(payload_type::kAck);
      expected_messages.insert(payload_type::kError);
    }
    else if (m_state > state::opened && m_options & option::ping) {
      expected_messages.insert(payload_type::kPong);
    }
    return expected_messages.find(message.payload_case()) != expected_messages.end();
  };
  if (!is_message_expected(message)) {
    error_code = error::code::protocol_error;
  }
  else {
    using payload_type = rstream::nperf::protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kAck) {
      on_handshake(boost::system::error_code());
    }
    else if (message_type == payload_type::kError) {
      error_code = error::make_error_code(message.error().code());
    }
    else if (message_type == payload_type::kPong) {
      on_pong(message.pong().data());
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_write(loop);
  }
}

void client::impl::base::session::do_send_protobuf_message(const rstream::nperf::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
#ifdef DEBUG_BUILD
  m_logger->trace("sending message to peer\n{}", core::helpers::to_json_string(message));
#endif
  std::size_t buffer_size = message.ByteSizeLong();
  auto buffer             = rstream::core::make_buffer_allocated(buffer_size);
  message.SerializeToArray(buffer.map().get_data(), buffer_size);
  auto completion_handler = std::bind(&session::on_send_protobuf_message, shared_from_this(), std::placeholders::_1, loop);
  m_payloader->async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::base::session::on_send_protobuf_message(const boost::system::error_code& error_code, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    do_read_write(loop);
  }
}

void client::impl::base::session::on_handshake(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
    return;
  }
  m_handshake.second = timestamp::clock::now();
  if (error_code) {
    on_error(error_code);
  }
  else {
    on_open();
  }
}

void client::impl::base::session::on_open()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opening) {
    return;
  }
  m_state = state::opened;
  if (m_open_callbacks.m_on_connection_handshake_us_cb) {
    auto connection_us = std::max((std::uint64_t)0, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(m_connection.second - m_connection.first).count()));
    auto handshake_us  = std::max((std::uint64_t)0, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(m_handshake.second - m_handshake.first).count()));
    m_open_callbacks.m_on_connection_handshake_us_cb(connection_us, handshake_us);
  }
  m_open_callbacks = {};
  if (m_handler_open) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler_open), boost::system::error_code());
  }
  m_handler_open = nullptr;
}

void client::impl::base::session::async_run_internal(const run_callbacks& run_callbacks, async_run_completion_handler&& handler)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opened) {
    if (handler) {
      rstream::core::invoke_completion_handler(m_executor, std::move(handler), error::code::invalid_state);
    }
    return;
  }
  m_run_callbacks = run_callbacks;
  m_handler_run.swap(handler);
  m_state = state::running;
  run_loop();
}

void client::impl::base::session::run_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer.reset_size();
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

void client::impl::base::session::ping_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_write(loop::send_ping);
  if (m_websocket) {
    do_read_write(loop::read_dummy);
  }
}

void client::impl::base::session::download_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_write(loop::read_data);
}

void client::impl::base::session::upload_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  rstream::core::random_bytes(m_buffer.get_data(), m_buffer.get_size());
  do_read_write(loop::write_data);
  do_read_write(loop::read_dummy);
}

void client::impl::base::session::do_read_write(enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running && m_state != state::closing) {
    return;
  }
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
    if (m_state == state::closing) {
      return;
    }
    auto buffer = boost::asio::const_buffer(m_buffer.get_data(), m_buffer.get_size());
    if (m_websocket) {
      m_websocket->async_write(buffer, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
    }
    else {
      m_socket.async_write_some(buffer, boost::asio::bind_executor(m_strand, completion_handler));
    }
  }
  else if (loop == loop::send_ping) {
    if (m_ping.m_active || m_ping.m_busy || m_state == state::closing) {
      return;
    }
    m_ping.m_active    = true;
    m_ping.m_busy      = true;
    m_ping.m_timestamp = timestamp::clock::now();
    auto randchar      = []() -> char {
      const char set[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
      return set[std::rand() % (sizeof(set) - 1)];
    };
    auto ping_buffer_size = std::min(m_ping.m_data.static_capacity, (std::size_t)m_settings.m_ping_buffer_size);
    m_ping.m_data         = boost::beast::websocket::ping_data(ping_buffer_size, 0);
    std::generate_n(m_ping.m_data.begin(), ping_buffer_size, randchar);
    if (m_websocket) {
      m_websocket->async_ping(m_ping.m_data, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, std::bind(completion_handler, std::placeholders::_1, 0)));
    }
    else if (m_payloader) {
      rstream::nperf::protobuf::Message message;
      message.mutable_ping()->set_data(m_ping.m_data.c_str());
      do_send_protobuf_message(message, loop::recv_ping);
    }
  }
  else if (loop == loop::recv_ping) {
    m_ping.m_busy = false;
    if (m_payloader) {
      do_read_incoming_protobuf_message(loop::null);
    }
  }
}

void client::impl::base::session::on_read_write(const boost::system::error_code& error_code, std::size_t bytes_transferred, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running) {
    return;
  }
  if (loop == loop::send_ping) {
    m_ping.m_busy = false;
  }
  if (!error_code) {
    if (loop == loop::read_data || loop == loop::write_data) {
      if (m_run_callbacks.m_on_bytes_transferred_cb) {
        m_run_callbacks.m_on_bytes_transferred_cb(bytes_transferred);
      }
    }
    if (loop != loop::send_ping) {
      do_read_write(loop);
    }
    else if (!m_ping.m_active) {
      do_read_write(loop);
    }
  }
  else {
    on_close(error_code == boost::beast::websocket::error::closed ? boost::system::error_code() : error_code);
  }
}

void client::impl::base::session::on_control_callback(boost::beast::websocket::frame_type kind, const boost::beast::string_view& payload)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running) {
    return;
  }
  // see : https://www.boost.org/doc/libs/1_68_0/libs/beast/example/advanced/server/advanced_server.cpp
  if (kind == boost::beast::websocket::frame_type::pong) {
    on_pong(payload);
  }
}

void client::impl::base::session::on_pong(const std::string& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!(m_options & option::ping)) {
    return;
  }
  if (!m_ping.m_active) {
    return;
  }
  if (m_ping.m_data.compare(data) != 0) {
    return;
  }
  m_ping.m_active = false;
  auto now        = timestamp::clock::now();
  auto ping       = std::max((std::uint64_t)0, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now - m_ping.m_timestamp).count()));
  if (m_run_callbacks.m_on_ping_cb) {
    m_run_callbacks.m_on_ping_cb(ping);
  }
  if (m_state == state::closing) {
    on_close(boost::system::error_code());
  }
  else if (!m_ping.m_busy) {
    do_read_write(loop::send_ping);
  }
}

void client::impl::base::session::cancel_internal(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  if (m_state == state::opened || m_state == state::running) {
    do_close(error_code);
  }
  else if (m_state == state::opening || m_state == state::closing) {
    on_close(error_code);
  }
}

void client::impl::base::session::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  on_close(error_code);
}

void client::impl::base::session::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::opened && m_state != state::running) {
    return;
  }
  m_state = state::closing;
  if (error_code && !m_error_code) {
    m_error_code = error_code;
  }
  auto completion_handler = std::bind(&session::on_close, shared_from_this(), std::placeholders::_1);
  if (m_websocket) {
    m_websocket->async_close(boost::beast::websocket::close_code::normal, boost::asio::bind_executor(boost::asio::any_io_executor{m_strand}, completion_handler));
  }
  else {
    bool ongoing_ops = false;
    if (m_options & option::ping) {
      if (m_ping.m_active) {
        ongoing_ops = true;
      }
    }
    if (!ongoing_ops) {
      completion_handler(boost::system::error_code());
    }
  }
}

void client::impl::base::session::on_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::closed) {
    return;
  }
  if (!m_error_code && !error_code && m_state != state::closing) {
    m_error_code = error::code::unexpected_close;
  }
  auto cause       = m_error_code ? m_error_code : error_code;
  m_state          = state::closed;
  m_open_callbacks = {};
  m_run_callbacks  = {};
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
  }
  m_websocket = nullptr;
  m_payloader = nullptr;
  if (m_handler_open) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler_open), cause);
  }
  m_handler_open = nullptr;
  if (m_handler_run) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler_run), cause);
  }
  m_handler_run = nullptr;
  m_buffer      = nullptr;
}

sample compute_sample(const std::vector<std::uint64_t>& sample, sample::type type)
{
  struct sample result = {};
  result.m_type        = type;
  result.m_size        = sample.size();
  if (result.m_size > 0) {
    auto minmax       = std::minmax_element(sample.begin(), sample.end());
    result.m_min_us   = *minmax.first;
    result.m_max_us   = *minmax.second;
    auto sum          = std::accumulate(sample.begin(), sample.end(), 0);
    auto mean         = (double)sum / result.m_size;
    result.m_mean_us  = mean;
    auto sq_sum       = std::inner_product(sample.begin(), sample.end(), sample.begin(), 0);
    auto stdev        = std::sqrt((double)sq_sum / result.m_size - mean * mean);
    result.m_stdev_us = stdev;
  }
  else {
    result.m_min_us   = 0;
    result.m_max_us   = 0;
    result.m_mean_us  = 0;
    result.m_stdev_us = 0;
  }
  return result;
}

}  // namespace nperf
}  // namespace rstream
