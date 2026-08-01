// See LICENSE file in the project root for license information.

#include "server.hpp"

#include <chrono>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/consign.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/strand.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>

#include <rstream/config.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/asio.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/metrics.hpp>

#ifdef RSTREAM_WITH_GEOIP
#include <rstream/io/geoip.hpp>
#endif

#include "attribute.hpp"
#include "error.hpp"
#include "message.hpp"

namespace rstream {
namespace stun {

class RSTREAM_GNUC_INTERNAL server::impl : public std::enable_shared_from_this<impl> {
 public:
  impl(const executor_type& executor, const config& config, const settings_server& settings);

  virtual ~impl() = default;

  void async_run(async_run_completion_handler&& handler);

  void cancel();

 private:
  using timestamp = std::chrono::time_point<std::chrono::system_clock>;

  enum class state {
    null,
    running,
    stopped
  };

  void async_run_internal(async_run_completion_handler&& handler);

  void cancel_internal();

  void on_error(const boost::system::error_code& error_code);

  void do_close(const boost::system::error_code& error_code);

  void do_resolve_host();

  void on_do_resolve_host(const boost::system::error_code& error_code, const boost::asio::ip::udp::resolver::results_type results);

  void do_configure_socket(const boost::asio::ip::udp::endpoint& endpoint);

  void process_stun_message_loop();

  void do_read_stun_message();

  void on_message(const boost::system::error_code& error_code, std::size_t size);

  void on_stun_message(const boost::asio::ip::udp::endpoint& src, const message& message);

  void process_stun_message(const boost::asio::ip::udp::endpoint& src, const message& message);

  void process_stun_binding_request(const boost::asio::ip::udp::endpoint& src, const message& message);

  void send_stun_message(const boost::asio::ip::udp::endpoint& dst, const message& message);

  void on_send_stun_message(const boost::system::error_code& error_code, std::size_t size);

  /// our configuration
  const config m_config;

  /// additionnal parameters
  const settings_server m_settings;

  /// executor
  executor_type m_executor;

  /// we use a strand for asynchronous operations
  boost::asio::strand<executor_type> m_strand;

  /// network resolver
  boost::asio::ip::udp::resolver m_resolver;

  /// UDP socket
  boost::asio::ip::udp::socket m_socket;

  /// server state
  state m_state;

  /// completion handler, may be null
  async_run_completion_handler m_handler;

  /// buffer being used to receive message
  rstream::core::buffer m_buffer_read;

  /// endpoint sending us message
  boost::asio::ip::udp::endpoint m_endpoint_read;

  /// updated on incoming request
  timestamp m_request_timestamp;

  /// logger
  rstream::core::logger m_logger;

#ifdef RSTREAM_WITH_GEOIP
  /// geoip module
  rstream::io::geoip::ptr m_geoip;
#endif

  /// metrics
  struct {
    rstream::core::metrics::counter m_requests_total;
    rstream::core::metrics::summary m_requests_duration_seconds;
  } m_metrics;
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
    : m_config(config),
      m_settings(settings),
      m_executor(executor),
      m_strand(executor),
      m_resolver(executor),
      m_socket(executor),
      m_state(state::null),
      m_logger({"stun", "client", fmt::format("#{}", fmt::ptr(this))})
#ifdef RSTREAM_WITH_GEOIP
      ,
      m_geoip(config.m_geoip.m_enable ? std::make_shared<rstream::io::geoip>(rstream::io::geoip::config{.m_database_location = config.m_geoip.m_database_location}) : nullptr)
#endif
      ,
      m_metrics({
          .m_requests_total            = {"stun_server_requests_total", "Counter of STUN requests."},
          .m_requests_duration_seconds = {"stun_server_request_duration_seconds", "Duration of STUN requests."},
      })

{
  m_buffer_read = rstream::core::make_buffer_allocated(m_settings.m_mtu);
}

void server::impl::async_run(async_run_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_run_internal, shared_from_this(), std::move(handler)));
}

void server::impl::cancel()
{
  boost::asio::dispatch(m_strand, std::bind_front(&server::impl::cancel_internal, shared_from_this()));
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
  m_state = state::running;
  do_resolve_host();
}

void server::impl::cancel_internal()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_close(boost::system::error_code());
}

void server::impl::on_error(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!error_code) {
    return;
  }
  do_close(error_code);
}

void server::impl::do_close(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running) {
    return;
  }
  m_state = state::stopped;
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), error_code);
  }
  m_handler = nullptr;
  boost::system::error_code tmp;
  m_resolver.cancel();
  m_socket.close(tmp);
}

void server::impl::do_resolve_host()
{
  auto completion_handler = std::bind(&impl::on_do_resolve_host, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_resolver.async_resolve(m_config.m_host, m_config.m_port, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::on_do_resolve_host(const boost::system::error_code& error_code, const boost::asio::ip::udp::resolver::results_type results)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running) {
    return;
  }
  boost::asio::ip::udp::resolver::results_type::iterator it;
  auto cause = error_code;
  if (!cause) {
    for (it = results.begin(); it != results.end(); ++it) {
      if (m_config.m_inet4 && it->endpoint().address().is_v6()) {
        continue;
      }
      if (m_config.m_inet6 && it->endpoint().address().is_v4()) {
        continue;
      }
      break;
    }
    if (it == results.end()) {
      cause = boost::system::error_code(error::code::no_valid_endpoint);
    }
  }
  if (cause) {
    on_error(cause);
  }
  else {
    do_configure_socket(it->endpoint());
  }
}

void server::impl::do_configure_socket(const boost::asio::ip::udp::endpoint& endpoint)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  boost::system::error_code error_code;
  m_socket.open(endpoint.protocol(), error_code);
  if (!error_code) {
    if (endpoint.address().is_v6()) {
      boost::asio::ip::v6_only option(m_config.m_inet6);
      m_socket.set_option(option, error_code);
    }
    if (!error_code) {
      m_socket.bind(endpoint, error_code);
    }
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    m_logger->info("server is now accepting connections on {}", boost::lexical_cast<std::string>(endpoint));
    process_stun_message_loop();
  }
}

void server::impl::process_stun_message_loop()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  do_read_stun_message();
}

void server::impl::do_read_stun_message()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_buffer_read.reset_size();
  auto completion_handler = std::bind(&impl::on_message, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_socket.async_receive_from(rstream::core::helpers::mutable_memory_sequence(m_buffer_read), m_endpoint_read, boost::asio::bind_executor(m_strand, completion_handler));
}

void server::impl::on_message(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::running) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else {
    m_buffer_read.set_size(size);
    std::exception_ptr error;
    stun::is_stun_datagram(m_buffer_read, error);
    if (!error) {
      class message message;
      try {
        message = stun::message().parse(m_buffer_read);
      }
      catch (...) {
        error = std::current_exception();
      }
      if (!error) {
        on_stun_message(m_endpoint_read, message);
      }
    }
    if (error) {
      m_logger->warn("cannot parse incoming stun message [error: {}]", rstream::core::throwable::message(error));
    }
    if (error) {
      do_read_stun_message();
    }
  }
}

void server::impl::on_stun_message(const boost::asio::ip::udp::endpoint& src, const message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_logger->trace("received STUN message from {}:\n{}", boost::lexical_cast<std::string>(src), message.to_string());
  m_request_timestamp = timestamp::clock::now();
  boost::optional<std::string> geoip_country_code;
#ifdef RSTREAM_WITH_GEOIP
  if (m_geoip) {
    try {
      geoip_country_code = m_geoip->lookup(src.address()).m_country_iso_code;
    }
    catch (...) {
    }
  }
#endif
  if (geoip_country_code) {
    m_metrics.m_requests_total.labels({{"geoip_country_code", geoip_country_code.get()}}).increment();
  }
  else {
    m_metrics.m_requests_total.increment();
  }
  process_stun_message(src, message);
}

void server::impl::process_stun_message(const boost::asio::ip::udp::endpoint& src, const message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (message.get_header().get_stun_class() == stun_class::request && message.get_header().get_stun_method() == stun_method::binding) {
    process_stun_binding_request(src, message);
  }
  else {
    do_read_stun_message();
  }
}

void server::impl::process_stun_binding_request(const boost::asio::ip::udp::endpoint& src, const message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto builder = message_builder(stun_class::response_success, stun_method::binding, message.get_header().get_transaction_id());
  {
    attribute_value_xor_mapped_address attribute;
    boost::asio::ip::address address = src.address();
    if (address.is_v6()) {
      auto v6 = address.to_v6();
      if (v6.is_v4_mapped()) {
        address = boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, v6);
      }
    }
    attribute.set_address(address);
    attribute.get_port() = src.port();
    builder.add_attribute(attribute);
  }
  builder.add_software();
  builder.add_fingerprint();
  class message response;
  std::exception_ptr error;
  try {
    response = builder.build();
  }
  catch (...) {
    error = std::current_exception();
  }
  if (error) {
    m_logger->warn("cannot build outgoing stun message [error: {}]", rstream::core::throwable::message(error));
  }
  if (error) {
    do_read_stun_message();
  }
  else {
    send_stun_message(src, response);
  }
}

void server::impl::send_stun_message(const boost::asio::ip::udp::endpoint& dst, const message& message)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  auto memory             = message.serialize_to_memory();
  auto completion_handler = std::bind(&impl::on_send_stun_message, shared_from_this(), std::placeholders::_1, std::placeholders::_2);
  m_logger->trace("sending STUN message to {}:\n{}", boost::lexical_cast<std::string>(dst), message.to_string());
  m_socket.async_send_to(boost::asio::const_buffer(memory.get_const_data(), memory.get_size()), dst, boost::asio::bind_executor(m_strand, boost::asio::consign(completion_handler, memory)));
}

void server::impl::on_send_stun_message(const boost::system::error_code& error_code, std::size_t size)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)size;
  if (error_code) {
    on_error(error_code);
  }
  else {
    auto delay_s = std::chrono::duration_cast<std::chrono::microseconds>(timestamp::clock::now() - m_request_timestamp).count() / 1000000.0;
    m_metrics.m_requests_duration_seconds.observe(delay_s);
    do_read_stun_message();
  }
}

}  // namespace stun
}  // namespace rstream
