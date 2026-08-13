// See LICENSE file in the project root for license information.

#include "client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/steady_timer.hpp>

#include <openssl/sha.h>
#ifndef RSTREAM_WITH_IO_STREAMS
#include <boost/asio/ip/tcp.hpp>
#endif
#ifdef _WIN32
#include <rstream/core/windows/blocking_handle.hpp>
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
#include <rstream/core/detail/protobuf.hpp>
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
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <rstream/webtty/protobuf/messages.pb.h>

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
namespace webtty {

namespace {

bool is_same_terminal_size(const terminal_size& lhs, const terminal_size& rhs)
{
  return lhs.m_row == rhs.m_row && lhs.m_col == rhs.m_col && lhs.m_xpixel == rhs.m_xpixel && lhs.m_ypixel == rhs.m_ypixel;
}

byte_vector bytes_from_string(const std::string& value)
{
  return byte_vector(value.begin(), value.end());
}

std::string string_from_bytes(const byte_vector& value)
{
  return std::string(value.begin(), value.end());
}

std::string string_from_value(const google::protobuf::StringValue& value)
{
  return value.value();
}

std::error_code map_protocol_error(rstream::webtty::protobuf::ProtocolErrorCode code)
{
  switch (code) {
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_UNKNOWN_SERVER:
      return error::code::known_server_required;
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_SERVER_KEY_CHANGED:
      return error::code::server_endpoint_identity_mismatch;
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_SERVER_PROOF_INVALID:
      return error::code::server_proof_invalid;
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_PROOF_REQUIRED:
      return error::code::client_proof_required;
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_PROOF_INVALID:
      return error::code::client_proof_invalid;
    case rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_UNAUTHORIZED:
      return error::code::client_unauthorized;
    default:
      return error::code::protocol_error;
  }
}

void to_proto(rstream::webtty::protobuf::KeyEnvelope& dst, const key_envelope& src)
{
  dst.set_recipient_key_id(string_from_bytes(src.m_recipient_key_id));
  dst.set_encapsulated_key(string_from_bytes(src.m_encapsulated_key));
  dst.set_wrapped_key(string_from_bytes(src.m_wrapped_key));
}

void to_proto(rstream::webtty::protobuf::SessionKeyGrant& dst, const session_key_grant& src)
{
  dst.set_payload_suite(static_cast<rstream::webtty::protobuf::PayloadCipherSuite>(static_cast<int>(src.m_payload_suite)));
  dst.set_payload_key_id(string_from_bytes(src.m_payload_key_id));
  for (const auto& envelope : src.m_key_envelopes) {
    to_proto(*dst.add_key_envelopes(), envelope);
  }
  dst.set_key_context(string_from_bytes(src.m_key_context));
  dst.set_key_envelope_suite(static_cast<rstream::webtty::protobuf::KeyEnvelopeSuite>(static_cast<int>(src.m_key_envelope_suite)));
}

void to_proto(rstream::webtty::protobuf::PayloadCrypto& dst, const payload_crypto_metadata& src)
{
  dst.set_payload_suite(static_cast<rstream::webtty::protobuf::PayloadCipherSuite>(static_cast<int>(src.m_payload_suite)));
  dst.set_payload_key_id(string_from_bytes(src.m_payload_key_id));
  dst.set_nonce(string_from_bytes(src.m_nonce));
  dst.set_aad_context(string_from_bytes(src.m_aad_context));
}

void to_proto(rstream::webtty::protobuf::EncryptedPayload& dst, const encrypted_payload& src)
{
  dst.set_ciphertext(string_from_bytes(src.m_ciphertext));
  dst.set_plaintext_length(src.m_plaintext_length);
  to_proto(*dst.mutable_payload_crypto(), src.m_payload_crypto);
}

void from_proto(payload_crypto_metadata& dst, const rstream::webtty::protobuf::PayloadCrypto& src)
{
  dst.m_payload_suite  = static_cast<payload_cipher_suite>(src.payload_suite());
  dst.m_payload_key_id = bytes_from_string(src.payload_key_id());
  dst.m_nonce          = bytes_from_string(src.nonce());
  dst.m_aad_context    = bytes_from_string(src.aad_context());
}

void from_proto(encrypted_payload& dst, const rstream::webtty::protobuf::EncryptedPayload& src)
{
  dst.m_ciphertext       = bytes_from_string(src.ciphertext());
  dst.m_plaintext_length = src.plaintext_length();
  if (src.has_payload_crypto()) {
    from_proto(dst.m_payload_crypto, src.payload_crypto());
  }
}

std::string transport_string(protocol::type value)
{
  switch (value) {
    case protocol::type::plain:
      return "plain";
    case protocol::type::websocket:
      return "websocket";
    default:
      return "";
  }
}

byte_vector sha256_bytes(const std::string& value)
{
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
  return byte_vector(digest, digest + SHA256_DIGEST_LENGTH);
}

bool hash_proto_message(byte_vector& dst, const google::protobuf::Message& message)
{
  std::string bytes;
  bytes.resize(message.ByteSizeLong());
  if (!bytes.empty()) {
    google::protobuf::io::ArrayOutputStream array_stream(bytes.data(), static_cast<int>(bytes.size()));
    google::protobuf::io::CodedOutputStream coded_stream(&array_stream);
    coded_stream.SetSerializationDeterministic(true);
    if (!message.SerializeToCodedStream(&coded_stream) || coded_stream.HadError()) {
      return false;
    }
  }
  dst = sha256_bytes(bytes);
  return true;
}

std::string utc_now_rfc3339()
{
  auto now  = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

bool parse_digit(int& dst, const std::string& value, std::size_t offset)
{
  if (offset >= value.size() || !std::isdigit(static_cast<unsigned char>(value[offset]))) {
    return false;
  }
  dst = value[offset] - '0';
  return true;
}

bool parse_fixed_digits(int& dst, const std::string& value, std::size_t offset, std::size_t length)
{
  dst = 0;
  for (std::size_t i = 0; i < length; ++i) {
    int digit = 0;
    if (!parse_digit(digit, value, offset + i)) {
      return false;
    }
    dst = dst * 10 + digit;
  }
  return true;
}

bool is_leap_year(int year)
{
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int days_in_month(int year, int month)
{
  static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && is_leap_year(year)) {
    return 29;
  }
  return days[month - 1];
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day)
{
  year -= month <= 2;
  const auto era = (year >= 0 ? year : year - 399) / 400;
  const auto yoe = static_cast<unsigned>(year - era * 400);
  const auto doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

bool parse_rfc3339_utc(std::chrono::system_clock::time_point& dst, const std::string& value)
{
  if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
    return false;
  }
  int year   = 0;
  int month  = 0;
  int day    = 0;
  int hour   = 0;
  int minute = 0;
  int second = 0;
  if (!parse_fixed_digits(year, value, 0, 4) || !parse_fixed_digits(month, value, 5, 2) || !parse_fixed_digits(day, value, 8, 2) || !parse_fixed_digits(hour, value, 11, 2) || !parse_fixed_digits(minute, value, 14, 2) || !parse_fixed_digits(second, value, 17, 2)) {
    return false;
  }
  if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }
  const auto days          = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
  const auto total_seconds = days * 86400 + hour * 3600 + minute * 60 + second;
  dst                      = std::chrono::system_clock::time_point(std::chrono::seconds(total_seconds));
  return true;
}

std::string add_seconds_rfc3339(const std::string& value, int seconds)
{
  std::chrono::system_clock::time_point parsed;
  if (!parse_rfc3339_utc(parsed, value)) {
    return {};
  }
  const auto raw = std::chrono::system_clock::to_time_t(parsed + std::chrono::seconds(seconds));
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &raw);
#else
  gmtime_r(&raw, &tm);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
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
  using stream_type = rstream::core::windows::blocking_handle;
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

  void on_connect(const std::error_code& error_code, const resolver_type::results_type::endpoint_type&);

  void do_handshake_websocket();

  void on_handshake_websocket(const std::error_code& error_code);

  void do_open();

  void do_send_open(const boost::optional<rstream::webtty::protobuf::ServerHello>& server_hello);

  void on_open();

  void cancel_internal();

  void on_error(const std::error_code& error_code);

  void do_close(const std::error_code& error_code);

  void on_close(const std::error_code& error_code, int code = 0);

  void on_queue_cancelled(const boost::system::error_code& error_code);

  void close_resources();

  void run_loop();

  void on_cmd_complete(int code);

  void finish_cmd_if_idle();

  void do_close_websocket(int code);

  void on_close_websocket(const std::error_code& error_code, int code);

  void read_std_in_loop();

  void do_read_std_in();

  void on_read_std_in(const std::error_code& error_code, std::size_t size);

  void read_terminal_size_loop();

  void do_wait_for_terminal_size();

  void do_send_message(const rstream::webtty::protobuf::Message& message, enum loop loop = loop::null);

  void on_send_message(const std::error_code& error_code, enum loop loop);

  void on_terminal_size_signal(const std::error_code& error_code, int);

  void on_wait_for_terminal_size(const std::error_code& error_code);

  void on_terminal_size(const terminal_size& terminal_size);

  void process_incoming_messages_loop();

  void do_read_incoming_message();

  void on_read_incoming_data(const std::error_code& error_code);

  void on_read_incoming_message(const rstream::webtty::protobuf::Message& message);

  std::error_code verify_server_hello(const rstream::webtty::protobuf::ServerHello& hello);

  std::error_code add_client_proof(rstream::webtty::protobuf::Open& open, const rstream::webtty::protobuf::ServerHello& hello);

  void do_process_data(const rstream::webtty::protobuf::Data& data);

  void do_process_data(const std::shared_ptr<std::string>& buffer, stdfd_type type);

  void on_process_data(const std::error_code& error_code);

  void send_heartbeat();

  void do_send_heartbeat();

  static bool is_message_expected(state state, const rstream::webtty::protobuf::Message& message);

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

  boost::asio::steady_timer m_terminal_size_timer;

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

  boost::optional<rstream::webtty::protobuf::ServerHello> m_server_hello;

  std::size_t m_pending_messages = 0;

  boost::optional<int> m_remote_return_code;
};

client::client(const executor_type& executor, const config& config, const settings_client& settings)
{
  m_impl = std::make_shared<impl>(executor, config, settings);
}

client::~client() noexcept
{
  try {
    cancel();
  }
  catch (...) {
    return;
  }
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
      m_stream_std_in(executor),
      m_stream_std_out(executor),
      m_stream_std_err(executor),
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
#ifdef _WIN32
  boost::system::error_code error_code;
  m_stream_std_out.open(::GetStdHandle(STD_OUTPUT_HANDLE), stream_type::access::write, error_code);
  if (!error_code) {
    m_stream_std_err.open(::GetStdHandle(STD_ERROR_HANDLE), stream_type::access::write, error_code);
  }
  if (!error_code && m_config.m_protocol_config.m_options.m_interactive) {
    m_stream_std_in.open(::GetStdHandle(STD_INPUT_HANDLE), stream_type::access::read, error_code);
  }
  if (error_code) {
    throw boost::system::system_error(error_code);
  }
#endif
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
  auto on_timer_cb         = [task_ptr, ptr](const std::error_code& error_code) {
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

void client::impl::on_connect(const std::error_code& error_code, const resolver_type::results_type::endpoint_type&)
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
      do_handshake_websocket();
    }
    else {
      do_open();
    }
  }
}

void client::impl::do_handshake_websocket()
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
  auto auth_token = m_config.m_auth_token;
  auto decorator  = [auth_token](boost::beast::websocket::request_type& request) {
    request.set(boost::beast::http::field::user_agent, "rstream websocket-client-async");
    if (auth_token) {
      request.set(boost::beast::http::field::authorization, "Bearer " + *auth_token);
    }
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
  if (m_settings.m_payload_crypto && !m_settings.m_expected_server_identity) {
    on_error(error::code::known_server_required);
    return;
  }
  if (m_settings.m_payload_crypto && !m_settings.m_endpoint_identity) {
    on_error(error::code::client_identity_required);
    return;
  }
  if (m_settings.m_expected_server_identity && !m_server_hello) {
    do_read_incoming_message();
    return;
  }
  do_send_open(m_server_hello);
}

void client::impl::do_send_open(const boost::optional<rstream::webtty::protobuf::ServerHello>& server_hello)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::connecting) {
    return;
  }
  rstream::webtty::protobuf::Message message;
  auto open   = message.mutable_open();
  auto config = m_config.m_protocol_config;
  if (config.m_options.m_allocate_tty) {
    protocol::add_environment_variable(config.m_env_vars, "TERM");
  }
  detail::convert(*open->mutable_config(), config);
  if (m_settings.m_payload_crypto) {
    std::error_code error_code;
    session_key_grant grant;
    m_settings.m_payload_crypto->get_session_key_grant(grant, error_code);
    if (error_code) {
      on_error(error_code);
      return;
    }
    open->add_capabilities(rstream::webtty::protobuf::OPEN_CAPABILITY_ENCRYPTED_PAYLOAD);
    open->add_capabilities(rstream::webtty::protobuf::OPEN_CAPABILITY_SESSION_CRYPTO);
    to_proto(*open->mutable_session_key_grant(), grant);
  }
  if (server_hello) {
    auto error_code = add_client_proof(*open, *server_hello);
    if (error_code) {
      on_error(error_code);
      return;
    }
  }
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
  rstream::webtty::protobuf::Message message;
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
  auto completion_handler = std::bind(&client::impl::on_queue_cancelled, shared_from_this(), std::placeholders::_1);
  m_queue->async_cancel(boost::asio::bind_executor(m_strand, std::move(completion_handler)));
}

void client::impl::on_queue_cancelled(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)error_code;
  close_resources();
}

void client::impl::close_resources()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  {
    boost::system::error_code tmp;
    m_resolver.cancel();
    m_socket.close(tmp);
#ifdef _WIN32
    m_stream_std_in.close();
    m_stream_std_out.close();
    m_stream_std_err.close();
#else
    m_stream_std_in.close(tmp);
    m_stream_std_out.close(tmp);
    m_stream_std_err.close(tmp);
#endif
    m_terminal_size_timer.cancel();
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
  if ((m_state != state::connected && m_state != state::disconnecting) || m_remote_return_code) {
    return;
  }
  if (m_state == state::connected) {
    set_state(state::disconnecting);
    arm_state_timer(m_settings.m_common.m_timeouts_ms.m_close);
  }
  m_remote_return_code = code;
  finish_cmd_if_idle();
}

void client::impl::finish_cmd_if_idle()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state != state::disconnecting || !m_remote_return_code || m_pending_messages != 0) {
    return;
  }
  if (m_websocket) {
    do_close_websocket(m_remote_return_code.get());
  }
  else {
    on_close(std::error_code(), m_remote_return_code.get());
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
  if (m_state != state::connected) {
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
  if (m_state != state::connected) {
    return;
  }
  bool eos = false;
  if (error_code) {
    eos = core::helpers::is_eof_error(error_code);
  }
  if (error_code && !eos) {
    on_error(error_code);
  }
  else {
    rstream::webtty::protobuf::Message message;
    auto data = message.mutable_data();
    data->set_type(rstream::webtty::protobuf::Data_Type_TYPE_STDIN);
    if (eos) {
      data->mutable_eos();
    }
    else if (m_settings.m_payload_crypto) {
      std::error_code crypto_error;
      encrypted_payload encrypted;
      const auto begin = static_cast<const unsigned char*>(m_buffer_std_in.map().get_const_data());
      const byte_vector plaintext(begin, begin + size);
      m_settings.m_payload_crypto->encrypt(payload_stream::std_in, plaintext, encrypted, crypto_error);
      if (crypto_error) {
        on_error(crypto_error);
        return;
      }
      to_proto(*data->mutable_encrypted_data(), encrypted);
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
  if (m_state != state::connected) {
    return;
  }
#ifdef _WIN32
  m_terminal_size_timer.expires_after(std::chrono::milliseconds(300));
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
  if (m_state != state::connected) {
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
  rstream::webtty::protobuf::Message message;
  detail::convert(*message.mutable_parameter()->mutable_terminal_size(), terminal_size);
  do_send_message(message, loop::read_terminal_size);
}

void client::impl::do_send_message(const rstream::webtty::protobuf::Message& message, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  rstream::core::buffer buffer;
  if (!rstream::core::detail::serialize_protobuf_message(message, buffer)) {
    on_error(error::code::protocol_error);
    return;
  }
  auto completion_handler = std::bind(&impl::on_send_message, shared_from_this(), std::placeholders::_1, loop);
  ++m_pending_messages;
  m_queue->async_send(buffer, boost::asio::bind_executor(m_strand, completion_handler));
}

void client::impl::on_send_message(const std::error_code& error_code, enum loop loop)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  assert(m_pending_messages != 0);
  --m_pending_messages;
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (error_code) {
    on_error(error_code);
  }
  else if (m_remote_return_code) {
    finish_cmd_if_idle();
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
  auto self               = shared_from_this();
  auto completion_handler = std::bind(&impl::on_read_incoming_data, self, std::placeholders::_1);
  if (m_websocket) {
    auto handler = [self, completion_handler](const std::error_code& error_code, std::size_t bytes_transferred) mutable {
      boost::asio::dispatch(self->m_strand, [self, completion_handler = std::move(completion_handler), error_code, bytes_transferred]() mutable {
        self->m_buffer_socket.set_size(bytes_transferred);
        completion_handler(error_code);
      });
    };
    m_http_buffers_adaptor = boost::beast::buffers_adaptor<core::helpers::mutable_memory_sequence>(core::helpers::mutable_memory_sequence(m_buffer_socket));
    m_websocket->async_read(m_http_buffers_adaptor, std::move(handler));
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
    rstream::webtty::protobuf::Message message;
    if (core::detail::parse_protobuf_message(message, m_buffer_socket.map().get_const_data(), m_buffer_socket.get_size())) {
      on_read_incoming_message(message);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

std::error_code client::impl::verify_server_hello(const rstream::webtty::protobuf::ServerHello& hello)
{
  if (!m_settings.m_expected_server_identity) {
    return {};
  }
  const auto& expected = *m_settings.m_expected_server_identity;
  if (!hello.has_server_identity() || !hello.has_server_proof()) {
    return error::code::server_proof_invalid;
  }
  const auto& actual = hello.server_identity();
  if (bytes_from_string(actual.encryption_key_id()) != expected.m_encryption_key_id || bytes_from_string(actual.encryption_public_key()) != expected.m_encryption_public_key || bytes_from_string(actual.signing_key_id()) != expected.m_signing_key_id || bytes_from_string(actual.signing_public_key()) != expected.m_signing_public_key) {
    return error::code::server_endpoint_identity_mismatch;
  }
  server_proof_transcript transcript;
  transcript.m_transport                = transport_string(m_config.m_protocol_config.m_protocol_type);
  transcript.m_workspace_id             = string_from_value(hello.workspace_id());
  transcript.m_project_id               = string_from_value(hello.project_id());
  transcript.m_server_id                = string_from_value(hello.server_id());
  transcript.m_session_id               = hello.session_id();
  transcript.m_server_signing_key_id    = bytes_from_string(actual.signing_key_id());
  transcript.m_server_encryption_key_id = bytes_from_string(actual.encryption_key_id());
  transcript.m_server_nonce             = bytes_from_string(hello.session_nonce());
  transcript.m_auth_requirement         = hello.auth_requirement() == rstream::webtty::protobuf::AUTH_REQUIREMENT_CLIENT_PROOF ? auth_requirement::client_proof : auth_requirement::none;
  for (auto value : hello.payload_suites()) {
    transcript.m_payload_suites.push_back(static_cast<payload_cipher_suite>(value));
  }
  for (auto value : hello.key_envelope_suites()) {
    transcript.m_key_envelope_suites.push_back(static_cast<key_envelope_suite>(value));
  }
  for (auto value : hello.signature_suites()) {
    transcript.m_signature_suites.push_back(static_cast<signature_suite>(value));
  }
  byte_vector expected_hash;
  std::error_code error_code;
  hash_webtty_server_proof_transcript(expected_hash, transcript, error_code);
  if (error_code) {
    return error_code;
  }
  const auto& proof = hello.server_proof();
  if (expected_hash != bytes_from_string(proof.transcript_hash())) {
    return error::code::server_proof_invalid;
  }
  verify_webtty_server_proof_transcript(expected.m_signing_public_key, transcript, bytes_from_string(proof.signature()), error_code);
  if (error_code) {
    return error::code::server_proof_invalid;
  }
  return {};
}

std::error_code client::impl::add_client_proof(rstream::webtty::protobuf::Open& open, const rstream::webtty::protobuf::ServerHello& hello)
{
  if (hello.auth_requirement() != rstream::webtty::protobuf::AUTH_REQUIREMENT_CLIENT_PROOF) {
    return {};
  }
  if (!m_settings.m_endpoint_identity) {
    return error::code::client_identity_required;
  }
  if (!hello.has_server_identity()) {
    return error::code::server_proof_invalid;
  }
  auto issued_at  = utc_now_rfc3339();
  auto expires_at = add_seconds_rfc3339(issued_at, 30);
  client_proof_transcript transcript;
  transcript.m_transport                = transport_string(m_config.m_protocol_config.m_protocol_type);
  transcript.m_workspace_id             = string_from_value(hello.workspace_id());
  transcript.m_project_id               = string_from_value(hello.project_id());
  transcript.m_server_id                = string_from_value(hello.server_id());
  transcript.m_session_id               = hello.session_id();
  transcript.m_server_signing_key_id    = bytes_from_string(hello.server_identity().signing_key_id());
  transcript.m_server_encryption_key_id = bytes_from_string(hello.server_identity().encryption_key_id());
  transcript.m_server_nonce             = bytes_from_string(hello.session_nonce());
  transcript.m_auth_requirement         = auth_requirement::client_proof;
  transcript.m_payload_suite            = payload_cipher_suite::aes_256_gcm;
  transcript.m_key_envelope_suite       = key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  if (open.has_session_key_grant()) {
    if (!hash_proto_message(transcript.m_session_key_grant_hash, open.session_key_grant())) {
      return error::code::protocol_error;
    }
  }
  else {
    transcript.m_session_key_grant_hash = sha256_bytes("");
  }
  if (open.has_config()) {
    if (!hash_proto_message(transcript.m_command_config_hash, open.config())) {
      return error::code::protocol_error;
    }
  }
  else {
    transcript.m_command_config_hash = sha256_bytes("");
  }
  transcript.m_client_principal_id    = m_settings.m_client_principal_id;
  transcript.m_client_signing_key_id  = m_settings.m_endpoint_identity->m_signing.m_key_id;
  transcript.m_client_credential_hash = m_settings.m_client_credential.empty() ? sha256_bytes("") : sha256_bytes(string_from_bytes(m_settings.m_client_credential));
  transcript.m_issued_at              = issued_at;
  transcript.m_expires_at             = expires_at;
  byte_vector transcript_hash;
  byte_vector signature;
  std::error_code error_code;
  sign_webtty_client_proof_transcript(transcript_hash, signature, m_settings.m_endpoint_identity->m_signing, transcript, error_code);
  if (error_code) {
    return error_code;
  }
  auto proof = open.mutable_client_proof();
  if (!m_settings.m_client_principal_id.empty()) {
    proof->mutable_principal_id()->set_value(m_settings.m_client_principal_id);
  }
  proof->set_signing_key_id(string_from_bytes(m_settings.m_endpoint_identity->m_signing.m_key_id));
  proof->set_signing_public_key(string_from_bytes(m_settings.m_endpoint_identity->m_signing.m_public_key));
  proof->set_signature_suite(rstream::webtty::protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  proof->set_transcript_hash(string_from_bytes(transcript_hash));
  proof->set_signature(string_from_bytes(signature));
  proof->set_issued_at(issued_at);
  proof->set_expires_at(expires_at);
  if (!m_settings.m_client_credential.empty()) {
    proof->mutable_credential()->set_value(string_from_bytes(m_settings.m_client_credential));
  }
  return {};
}

void client::impl::on_read_incoming_message(const rstream::webtty::protobuf::Message& message)
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
    using payload_type = rstream::webtty::protobuf::Message::PayloadCase;
    auto message_type  = message.payload_case();
    if (message_type == payload_type::kServerHello) {
      error_code = m_settings.m_expected_server_identity ? verify_server_hello(message.server_hello()) : error::make_error_code(error::code::known_server_required);
      if (!error_code) {
        m_server_hello = message.server_hello();
        do_open();
        return;
      }
    }
    else if (message_type == payload_type::kAck) {
      on_open();
    }
    else if (message_type == payload_type::kError) {
      error_code = error::make_error_code(error::code::server_error);
    }
    else if (message_type == payload_type::kProtocolError) {
      error_code = map_protocol_error(message.protocol_error().code());
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

void client::impl::do_process_data(const rstream::webtty::protobuf::Data& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  std::optional<stdfd_type> type;
  if (data.type() == rstream::webtty::protobuf::Data::TYPE_STDOUT) {
    type = stdfd_type::std_out;
  }
  else if (data.type() == rstream::webtty::protobuf::Data::TYPE_STDERR) {
    type = stdfd_type::std_err;
  }
  if (!type) {
    on_error(error::code::unexpected_message);
    return;
  }
  if (data.has_encrypted_data()) {
    if (!m_settings.m_payload_crypto) {
      on_error(error::code::protocol_error);
      return;
    }
    std::error_code crypto_error;
    encrypted_payload encrypted;
    byte_vector plaintext;
    from_proto(encrypted, data.encrypted_data());
    m_settings.m_payload_crypto->decrypt(*type == stdfd_type::std_out ? payload_stream::std_out : payload_stream::std_err, encrypted, plaintext, crypto_error);
    if (crypto_error) {
      on_error(crypto_error);
      return;
    }
    auto buffer = std::make_shared<std::string>(plaintext.begin(), plaintext.end());
    do_process_data(buffer, *type);
  }
  else {
    auto buffer = data.has_eos() ? nullptr : std::make_shared<std::string>(data.data());
    do_process_data(buffer, *type);
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
#ifdef _WIN32
    stream.close();
#else
    stream.close();
#endif
    on_process_data(std::error_code());
  }
  else {
    auto handler = [ptr = shared_from_this(), buffer](const std::error_code& error_code, std::size_t) {
      ptr->on_process_data(error_code);
    };
    auto boost_buffer = boost::asio::const_buffer(buffer->data(), buffer->size());
#ifdef _WIN32
    stream.async_write(boost_buffer, boost::asio::bind_executor(m_strand, handler));
#else
    boost::asio::async_write(stream, boost_buffer, boost::asio::bind_executor(m_strand, handler));
#endif
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
    boost::asio::steady_timer m_timer;
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
      rstream::webtty::protobuf::Message message;
      message.mutable_heartbeat();
      ptr->do_send_message(message, loop::heartbeat);
    }
    task_ptr->clean();
  };
  task_ptr->m_timer.expires_after(std::chrono::milliseconds(timeout_ms));
  auto completion_handler = boost::asio::bind_executor(ptr->m_strand, on_timer_cb);
  task_ptr->m_timer.async_wait(completion_handler);
}

bool client::impl::is_message_expected(state state, const rstream::webtty::protobuf::Message& message)
{
  using payload_type                                                                      = rstream::webtty::protobuf::Message::PayloadCase;
  static const std::map<client::impl::state, std::set<payload_type>> compatibility_matrix = {
      {state::connecting, {payload_type::kServerHello, payload_type::kAck, payload_type::kError, payload_type::kProtocolError}},
      {state::connected, {payload_type::kData, payload_type::kClose, payload_type::kError, payload_type::kProtocolError, payload_type::kHeartbeat}},
      {state::disconnecting, {payload_type::kData, payload_type::kClose, payload_type::kError, payload_type::kProtocolError, payload_type::kHeartbeat}},
  };
  const auto& set = compatibility_matrix.find(state)->second;
  return set.find(message.payload_case()) != set.end();
}

}  // namespace webtty
}  // namespace rstream
