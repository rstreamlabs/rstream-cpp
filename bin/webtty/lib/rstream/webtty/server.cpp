// See LICENSE file in the project root for license information.

#include "server.hpp"

#ifndef BOOST_PROCESS_VERSION
#define BOOST_PROCESS_VERSION 1
#elif BOOST_PROCESS_VERSION != 1
#error "rstream WebTTY requires Boost.Process v1"
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/steady_timer.hpp>

#include <openssl/rand.h>
#include <openssl/sha.h>
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

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <rstream/config.hpp>
#include <rstream/core/buffer.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/detail/protobuf.hpp>
#include <rstream/core/exception.hpp>
#include <rstream/core/helpers/protobuf.hpp>
#include <rstream/core/log.hpp>
#include <rstream/core/memory.hpp>
#include <rstream/core/object_id.hpp>
#include <rstream/io/payloader.hpp>
#include <rstream/io/queue.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#ifdef RSTREAM_WITH_IO_STREAMS
#include <rstream/io/detail/stream/websocket.hpp>
#include <rstream/io/stream.hpp>
#endif

#include "detail/convert.hpp"
#include "detail/process.hpp"
#include "error.hpp"
#include "stream.hpp"
#include "webtty.hpp"

namespace rstream {
namespace webtty {

static void parse_environment(boost::process::environment& dst, const protocol::env_vars& src);

namespace {

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

bool should_redact_query_key(std::string key)
{
  std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return key.find("token") != std::string::npos || key.find("secret") != std::string::npos || key.find("credential") != std::string::npos || key.find("api-key") != std::string::npos || key.find("apikey") != std::string::npos;
}

std::string redact_http_target(std::string target)
{
  const auto query_pos = target.find('?');
  if (query_pos == std::string::npos) {
    return target;
  }
  const auto fragment_pos = target.find('#', query_pos + 1);
  const auto query_end    = fragment_pos == std::string::npos ? target.size() : fragment_pos;
  const auto prefix       = target.substr(0, query_pos + 1);
  const auto query        = target.substr(query_pos + 1, query_end - query_pos - 1);
  const auto suffix       = fragment_pos == std::string::npos ? std::string() : target.substr(fragment_pos);
  std::stringstream redacted;
  std::size_t pos = 0;
  bool first      = true;
  while (pos <= query.size()) {
    const auto next = query.find('&', pos);
    const auto part = query.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
    if (!first) {
      redacted << '&';
    }
    first             = false;
    const auto eq_pos = part.find('=');
    const auto key    = eq_pos == std::string::npos ? part : part.substr(0, eq_pos);
    if (eq_pos != std::string::npos && should_redact_query_key(key)) {
      redacted << key << "=<redacted>";
    }
    else {
      redacted << part;
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }
  return prefix + redacted.str() + suffix;
}

bool to_protocol_error_code(rstream::webtty::protobuf::ProtocolErrorCode& dst, error::code code)
{
  switch (code) {
    case error::code::known_server_required:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_UNKNOWN_SERVER;
      return true;
    case error::code::server_endpoint_identity_mismatch:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_SERVER_KEY_CHANGED;
      return true;
    case error::code::server_proof_invalid:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_SERVER_PROOF_INVALID;
      return true;
    case error::code::client_proof_required:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_PROOF_REQUIRED;
      return true;
    case error::code::client_proof_invalid:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_PROOF_INVALID;
      return true;
    case error::code::client_unauthorized:
      dst = rstream::webtty::protobuf::PROTOCOL_ERROR_CODE_CLIENT_UNAUTHORIZED;
      return true;
    default:
      return false;
  }
}

payload_stream payload_stream_from_stream(stream::type type)
{
  return type == stream::type::std_out ? payload_stream::std_out : payload_stream::std_err;
}

void to_proto(rstream::webtty::protobuf::EndpointIdentity& dst, const endpoint_identity_public& src)
{
  dst.set_signing_key_id(string_from_bytes(src.m_signing_key_id));
  dst.set_signing_public_key(string_from_bytes(src.m_signing_public_key));
  dst.set_signature_suite(rstream::webtty::protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  dst.set_encryption_key_id(string_from_bytes(src.m_encryption_key_id));
  dst.set_encryption_public_key(string_from_bytes(src.m_encryption_public_key));
  dst.set_key_envelope_suite(rstream::webtty::protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
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

void from_proto(key_envelope& dst, const rstream::webtty::protobuf::KeyEnvelope& src)
{
  dst.m_recipient_key_id = bytes_from_string(src.recipient_key_id());
  dst.m_encapsulated_key = bytes_from_string(src.encapsulated_key());
  dst.m_wrapped_key      = bytes_from_string(src.wrapped_key());
}

void from_proto(session_key_grant& dst, const rstream::webtty::protobuf::SessionKeyGrant& src)
{
  dst.m_payload_suite  = static_cast<payload_cipher_suite>(src.payload_suite());
  dst.m_payload_key_id = bytes_from_string(src.payload_key_id());
  dst.m_key_envelopes.clear();
  for (const auto& envelope : src.key_envelopes()) {
    key_envelope converted;
    from_proto(converted, envelope);
    dst.m_key_envelopes.push_back(std::move(converted));
  }
  dst.m_key_context        = bytes_from_string(src.key_context());
  dst.m_key_envelope_suite = static_cast<key_envelope_suite>(src.key_envelope_suite());
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

std::string execution_mode_string(execution_mode value)
{
  switch (value) {
    case execution_mode::spawn:
      return "spawn";
    case execution_mode::login:
      return "login";
    default:
      return "";
  }
}

std::string session_error_reason_code(const std::error_code& error_code)
{
  if (!error_code) {
    return "";
  }
  if (error_code.category() == std::error_code(error::code{}).category()) {
    switch (static_cast<error::code>(error_code.value())) {
      case error::code::client_proof_required:
        return "client_proof_required";
      case error::code::client_proof_invalid:
        return "client_proof_invalid";
      case error::code::client_unauthorized:
        return "client_unauthorized";
      case error::code::operation_timeout:
        return "operation_timeout";
      case error::code::e2e_session_key_grant_required:
        return "session_key_grant_invalid";
      case error::code::unsupported_execution_mode:
        return "unsupported_execution_mode";
      case error::code::login_user_required:
        return "login_user_required";
      case error::code::client_user_disabled:
        return "client_user_disabled";
      case error::code::protocol_error:
      case error::code::unexpected_message:
        return "protocol_error";
      case error::code::operation_aborted:
        return "operation_aborted";
      default:
        return "session_error";
    }
  }
  auto message = error_code.message();
  std::transform(message.begin(), message.end(), message.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (message.find("timed out") != std::string::npos || message.find("timeout") != std::string::npos) {
    return "operation_timeout";
  }
  if (message.find("operation canceled") != std::string::npos || message.find("operation aborted") != std::string::npos) {
    return "operation_aborted";
  }
  return "session_error";
}

byte_vector sha256_bytes(const std::string& value)
{
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
  return byte_vector(digest, digest + SHA256_DIGEST_LENGTH);
}

std::string bytes_to_hex(const byte_vector& value)
{
  std::ostringstream oss;
  oss << std::hex << std::setfill('0');
  for (auto byte : value) {
    oss << std::setw(2) << static_cast<unsigned int>(byte);
  }
  return oss.str();
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

}  // namespace

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

  void on_queue_cancelled(const boost::system::error_code& error_code);

  void close_resources();

  void do_read_http_request();

  void on_read_http_request(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_process_http_request();

  void do_write_http_response(boost::beast::http::status status);

  void on_write_http_response(const boost::system::error_code& error_code, std::size_t bytes_transferred);

  void do_accept_websocket();

  void on_accept_websocket(const std::error_code& error_code);

  void do_open();

  void do_send_server_hello();

  void on_open(const protocol::config& protocol_config);

  void run_loop();

  void read_stdfd_loop();

  void do_read_stdfd(stream::type type);

  void on_read_stdfd(const std::error_code& error_code, std::size_t size, stream::type type);

  void do_send_error(const std::error_code& error_code);

  void do_send_close(int code);

  void do_send_message(const rstream::webtty::protobuf::Message& message, enum loop loop);

  void on_send_message(const std::error_code& error_code, enum loop loop);

  void process_incoming_messages_loop();

  void do_read_incoming_message();

  void on_read_incoming_data(const std::error_code& error_code);

  void on_read_incoming_message(const rstream::webtty::protobuf::Message& message);

  std::error_code verify_client_proof(const rstream::webtty::protobuf::Open& open);

  void do_process_data(const rstream::webtty::protobuf::Data& data);

  void do_process_data(const std::shared_ptr<std::string> buffer);

  void on_process_data(const std::error_code& error_code);

  void on_child_exit(const std::error_code& error_code, int code);

  void do_close_websocket();

  void on_close_websocket(const std::error_code& error_code);

  void set_terminal_size(const terminal_size& terminal_size, std::error_code& error_code);

  void send_heartbeat();

  void do_send_heartbeat();

  static bool is_message_expected(state state, const rstream::webtty::protobuf::Message& message);

  void log_session_accepted();

  void log_session_rejected(const std::error_code& error_code);

  void log_session_closed(const std::error_code& error_code);

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

  payload_crypto::ptr m_payload_crypto;

  byte_vector m_server_nonce;

  protocol::type m_protocol_type;

  std::chrono::steady_clock::time_point m_accepted_at;

  bool m_opened = false;

  bool m_authenticated = false;

  bool m_child_done = false;

  int m_child_exit_code = 0;

  std::string m_server_signing_key_id;

  std::string m_client_principal_id;

  std::string m_client_device_id;

  std::string m_client_browser_id;

  std::string m_client_signing_key_id;
};

server::server(const executor_type& executor, const config& config, const settings_server& settings)
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
      m_logger({"rstream", "webtty", "server", fmt::format("#{}", fmt::ptr(this))}),
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
      m_logger({"rstream", "webtty", "session", fmt::format("#{}", session_id)}),
      m_state(state::null),
      m_buffer_socket(rstream::core::make_buffer_allocated(m_settings.m_common.m_mtu)),
      m_buffer_std_out(rstream::core::make_buffer_allocated(m_settings.m_std_out_buffer_size)),
      m_buffer_std_err(rstream::core::make_buffer_allocated(m_settings.m_std_err_buffer_size)),
      m_http_buffers_adaptor(core::helpers::mutable_memory_sequence(m_buffer_socket)),
      m_protocol_type(protocol_type),
      m_accepted_at(std::chrono::steady_clock::now())
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

void server::impl::session::log_session_accepted()
{
  if (m_authenticated) {
    return;
  }
  m_authenticated = true;
  std::stringstream fields;
  fields << std::boolalpha;
  auto append = [&fields](const char* key, const std::string& value) {
    if (!value.empty()) {
      fields << ", " << key << ": " << value;
    }
  };
  fields << "session_id: " << m_session_id
         << ", transport: " << transport_string(m_protocol_type)
         << ", e2e: " << (m_settings.m_payload_crypto_resolver != nullptr)
         << ", client_proof_required: " << m_settings.m_require_client_proof
         << ", execution_mode: " << execution_mode_string(m_settings.m_execution_mode);
  append("workspace_id", m_settings.m_workspace_id);
  append("project_id", m_settings.m_project_id);
  append("server_id", m_settings.m_server_id);
  append("server_signing_key_id", m_server_signing_key_id);
  append("client_principal_id", m_client_principal_id);
  append("client_device_id", m_client_device_id);
  append("client_browser_id", m_client_browser_id);
  append("client_signing_key_id", m_client_signing_key_id);
  m_logger->info("session accepted [{}]", fields.str());
}

void server::impl::session::log_session_rejected(const std::error_code& error_code)
{
  if (m_opened) {
    return;
  }
  std::stringstream fields;
  fields << std::boolalpha;
  auto append = [&fields](const char* key, const std::string& value) {
    if (!value.empty()) {
      fields << ", " << key << ": " << value;
    }
  };
  fields << "session_id: " << m_session_id
         << ", transport: " << transport_string(m_protocol_type)
         << ", e2e: " << (m_settings.m_payload_crypto_resolver != nullptr)
         << ", client_proof_required: " << m_settings.m_require_client_proof
         << ", execution_mode: " << execution_mode_string(m_settings.m_execution_mode)
         << ", reason_code: " << session_error_reason_code(error_code)
         << ", error_code: " << (error_code ? error_code.message() : "none");
  append("workspace_id", m_settings.m_workspace_id);
  append("project_id", m_settings.m_project_id);
  append("server_id", m_settings.m_server_id);
  append("server_signing_key_id", m_server_signing_key_id);
  append("client_principal_id", m_client_principal_id);
  append("client_device_id", m_client_device_id);
  append("client_browser_id", m_client_browser_id);
  append("client_signing_key_id", m_client_signing_key_id);
  m_logger->warn("session rejected [{}]", fields.str());
}

void server::impl::session::log_session_closed(const std::error_code& error_code)
{
  const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_accepted_at).count();
  std::stringstream fields;
  fields << std::boolalpha;
  auto append = [&fields](const char* key, const std::string& value) {
    if (!value.empty()) {
      fields << ", " << key << ": " << value;
    }
  };
  fields << "session_id: " << m_session_id
         << ", transport: " << transport_string(m_protocol_type)
         << ", duration_ms: " << duration_ms
         << ", opened: " << m_opened
         << ", authenticated: " << m_authenticated
         << ", child_done: " << m_child_done
         << ", exit_code: " << m_child_exit_code
         << ", e2e: " << (m_settings.m_payload_crypto_resolver != nullptr)
         << ", client_proof_required: " << m_settings.m_require_client_proof
         << ", execution_mode: " << execution_mode_string(m_settings.m_execution_mode)
         << ", error_code: " << (error_code ? error_code.message() : "none");
  append("reason_code", session_error_reason_code(error_code));
  append("workspace_id", m_settings.m_workspace_id);
  append("project_id", m_settings.m_project_id);
  append("server_id", m_settings.m_server_id);
  append("server_signing_key_id", m_server_signing_key_id);
  append("client_principal_id", m_client_principal_id);
  append("client_device_id", m_client_device_id);
  append("client_browser_id", m_client_browser_id);
  append("client_signing_key_id", m_client_signing_key_id);
  m_logger->info("session closed [{}]", fields.str());
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
  log_session_closed(cause);
  if (m_handler) {
    rstream::core::invoke_completion_handler(m_executor, std::move(m_handler), cause);
  }
  m_handler               = nullptr;
  auto completion_handler = std::bind(&session::on_queue_cancelled, shared_from_this(), std::placeholders::_1);
  m_queue->async_cancel(boost::asio::bind_executor(m_strand, std::move(completion_handler)));
}

void server::impl::session::on_queue_cancelled(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  (void)error_code;
  close_resources();
}

void server::impl::session::close_resources()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  {
    boost::system::error_code tmp;
    m_socket.close(tmp);
  }
  if (m_stream_ptr) {
    m_stream_ptr->close();
  }
  if (m_child) {
    boost::system::error_code tmp;
    m_child->terminate(tmp);
    m_child->wait(tmp);
  }
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
    auto sanitized = m_http_request.base();
    sanitized.target(redact_http_target(std::string(sanitized.target())));
    auto redact_header = [&sanitized](boost::beast::http::field field) {
      if (sanitized.find(field) != sanitized.end()) {
        sanitized.set(field, "<redacted>");
      }
    };
    redact_header(boost::beast::http::field::authorization);
    redact_header(boost::beast::http::field::proxy_authorization);
    redact_header(boost::beast::http::field::cookie);
    std::stringstream str;
    str << sanitized;
    str.str(str.str().substr(0, str.str().find_last_not_of("\r\n") + 1));
    m_logger->trace("HTTP request :\n{}", str.str());
  }
#else
  m_logger->trace("HTTP request '{}' '{}'", std::string(m_http_request.method_string()), redact_http_target(std::string(m_http_request.target())));
#endif
  auto is_upgrade = false;
  // see if it is a websocket upgrade
  if (boost::beast::websocket::is_upgrade(m_http_request)) {
    if (m_http_request.method() == boost::beast::http::verb::get) {
      is_upgrade = true;
    }
  }
  if (is_upgrade) {
    if (m_settings.m_auth_token && std::string(m_http_request[boost::beast::http::field::authorization]) != "Bearer " + *m_settings.m_auth_token) {
      do_write_http_response(boost::beast::http::status::unauthorized);
      return;
    }
    do_accept_websocket();
  }
  else {
    do_write_http_response(boost::beast::http::status::bad_request);
  }
}

void server::impl::session::do_write_http_response(boost::beast::http::status status)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  m_http_response = boost::beast::http::response<boost::beast::http::empty_body>(status, m_http_request.version());
  if (status == boost::beast::http::status::unauthorized) {
    m_http_response.set(boost::beast::http::field::www_authenticate, "Bearer");
  }
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
  if (m_settings.m_payload_crypto_resolver && !m_settings.m_endpoint_identity) {
    on_error(error::code::protocol_error);
    return;
  }
  if (m_settings.m_payload_crypto_resolver && !m_settings.m_require_client_proof) {
    on_error(error::code::client_proof_required);
    return;
  }
  if (m_settings.m_endpoint_identity) {
    do_send_server_hello();
  }
  do_read_incoming_message();
}

void server::impl::session::do_send_server_hello()
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (!m_settings.m_endpoint_identity) {
    return;
  }
  m_server_nonce.assign(32, 0);
  if (RAND_bytes(m_server_nonce.data(), static_cast<int>(m_server_nonce.size())) != 1) {
    on_error(error::code::crypto_error);
    return;
  }
  auto public_identity    = public_endpoint_identity(*m_settings.m_endpoint_identity);
  m_server_signing_key_id = bytes_to_hex(public_identity.m_signing_key_id);
  server_proof_transcript transcript;
  transcript.m_transport                = transport_string(m_protocol_type);
  transcript.m_workspace_id             = m_settings.m_workspace_id;
  transcript.m_project_id               = m_settings.m_project_id;
  transcript.m_server_id                = m_settings.m_server_id;
  transcript.m_session_id               = m_session_id;
  transcript.m_server_signing_key_id    = public_identity.m_signing_key_id;
  transcript.m_server_encryption_key_id = public_identity.m_encryption_key_id;
  transcript.m_server_nonce             = m_server_nonce;
  transcript.m_auth_requirement         = m_settings.m_require_client_proof ? auth_requirement::client_proof : auth_requirement::none;
  transcript.m_payload_suites           = {payload_cipher_suite::aes_256_gcm};
  transcript.m_key_envelope_suites      = {key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm};
  transcript.m_signature_suites         = {signature_suite::ecdsa_p256_sha256};
  byte_vector transcript_hash;
  byte_vector signature;
  std::error_code error_code;
  sign_webtty_server_proof_transcript(transcript_hash, signature, m_settings.m_endpoint_identity->m_signing, transcript, error_code);
  if (error_code) {
    on_error(error_code);
    return;
  }
  rstream::webtty::protobuf::Message message;
  auto hello = message.mutable_server_hello();
  hello->set_protocol_version(rstream::webtty::protobuf::PROTOCOL_VERSION_WEBTTY_1);
  hello->set_session_nonce(string_from_bytes(m_server_nonce));
  to_proto(*hello->mutable_server_identity(), public_identity);
  hello->add_payload_suites(rstream::webtty::protobuf::PAYLOAD_CIPHER_SUITE_AES_256_GCM);
  hello->add_key_envelope_suites(rstream::webtty::protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
  hello->add_signature_suites(rstream::webtty::protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  hello->set_auth_requirement(m_settings.m_require_client_proof ? rstream::webtty::protobuf::AUTH_REQUIREMENT_CLIENT_PROOF : rstream::webtty::protobuf::AUTH_REQUIREMENT_NONE);
  if (!m_settings.m_workspace_id.empty()) {
    hello->mutable_workspace_id()->set_value(m_settings.m_workspace_id);
  }
  if (!m_settings.m_project_id.empty()) {
    hello->mutable_project_id()->set_value(m_settings.m_project_id);
  }
  if (!m_settings.m_server_id.empty()) {
    hello->mutable_server_id()->set_value(m_settings.m_server_id);
  }
  hello->set_session_id(m_session_id);
  auto proof = hello->mutable_server_proof();
  proof->set_signature_suite(rstream::webtty::protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  proof->set_signing_key_id(string_from_bytes(public_identity.m_signing_key_id));
  proof->set_transcript_hash(string_from_bytes(transcript_hash));
  proof->set_signature(string_from_bytes(signature));
  do_send_message(message, loop::null);
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
  auto effective_username = protocol_config.m_username;
  if (m_settings.m_execution_mode == execution_mode::login) {
    if (effective_username && !m_settings.m_allow_client_user) {
      m_logger->warn("client-selected OS user rejected in login execution mode");
      error_code = error::code::client_user_disabled;
    }
    if (!error_code && !effective_username && m_settings.m_default_username) {
      effective_username = m_settings.m_default_username;
    }
    if (!error_code && !effective_username) {
      m_logger->warn("login execution mode requires a configured default user or allowed client user");
      error_code = error::code::login_user_required;
    }
  }
  if (!error_code) {
    get_user_info(user_info, effective_username, error_code);
  }
  if (!error_code) {
    auto workdir = protocol_config.m_workdir ? protocol_config.m_workdir.get() : user_info.m_home;
    boost::process::environment environment;
    auto env_vars_copy = protocol_config.m_env_vars;
    protocol::add_execution_environment(env_vars_copy, m_settings.m_execution_mode, user_info);
    parse_environment(environment, env_vars_copy);
    auto completion_handler = [ptr = shared_from_this()](int code, const std::error_code& error_code) {
      boost::asio::post(ptr->m_strand, std::bind_front(&session::on_child_exit, ptr, error_code, code));
    };
    auto backend    = protocol_config.m_options.m_allocate_tty ? stream::backend::tty : stream::backend::pipe;
    const auto exe  = protocol_config.m_cmd_args.size() > 0 ? protocol_config.m_cmd_args.front() : user_info.m_shell;
    const auto args = protocol_config.m_cmd_args.size() > 1 ? protocol::cmd_args(std::next(protocol_config.m_cmd_args.begin()), protocol_config.m_cmd_args.end()) : protocol::cmd_args();
    m_stream_ptr    = stream::make_stream(m_executor, backend);
    m_logger->info("starting child process [session_id: {}, backend: {}, exe: {}, args_count: {}, workdir: {}, execution_mode: {}]",
                   m_session_id,
                   backend == stream::backend::tty ? "tty" : "pipe",
                   exe,
                   args.size(),
                   workdir,
                   execution_mode_string(m_settings.m_execution_mode));
    m_logger->trace("starting child process detail [session_id: {}, args_count: {}, env_count: {}]", m_session_id, args.size(), env_vars_copy.size());
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
    m_opened = true;
    set_state(state::connected);
    rstream::webtty::protobuf::Message message;
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
    if (core::helpers::is_eof_error(error_code)) {
      eos = true;
    }
#ifdef _WIN32
    // on windows pipe streams return ERROR_BROKEN_PIPE rather than eof to indicate end of file
    else if (core::helpers::matches_error(error_code, boost::system::error_code(boost::asio::error::broken_pipe))) {
      eos = m_stream_ptr->backend() == stream::backend::pipe || m_stream_ptr->backend() == stream::backend::tty;
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
    rstream::webtty::protobuf::Message message;
    auto data = message.mutable_data();
    data->set_type(type == stream::type::std_out ? rstream::webtty::protobuf::Data_Type_TYPE_STDOUT : rstream::webtty::protobuf::Data_Type_TYPE_STDERR);
    if (eos) {
      data->mutable_eos();
    }
    else if (m_payload_crypto) {
      std::error_code crypto_error;
      encrypted_payload encrypted;
      auto& buffer     = type == stream::type::std_out ? m_buffer_std_out : m_buffer_std_err;
      const auto begin = static_cast<const unsigned char*>(buffer.map().get_const_data());
      const byte_vector plaintext(begin, begin + size);
      m_payload_crypto->encrypt(payload_stream_from_stream(type), plaintext, encrypted, crypto_error);
      if (crypto_error) {
        on_error(crypto_error);
        return;
      }
      to_proto(*data->mutable_encrypted_data(), encrypted);
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
  log_session_rejected(error_code);
  set_state(state::disconnecting);
  rstream::webtty::protobuf::Message message;
  error::code code;
  if (error_code.category() == std::error_code(error::code{}).category()) {
    code = static_cast<error::code>(error_code.value());
  }
  else {
    code = error::code::unknown_undefined_error;
  }
  rstream::webtty::protobuf::ProtocolErrorCode protocol_error_code;
  if (to_protocol_error_code(protocol_error_code, code)) {
    auto protocol_error = message.mutable_protocol_error();
    protocol_error->set_code(protocol_error_code);
    protocol_error->set_msg(error_code.message());
  }
  else {
    message.mutable_error()->set_msg(error_code.message());
  }
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
    rstream::webtty::protobuf::Message message;
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

void server::impl::session::do_send_message(const rstream::webtty::protobuf::Message& message, enum loop loop)
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
  auto self               = shared_from_this();
  auto completion_handler = std::bind(&session::on_read_incoming_data, self, std::placeholders::_1);
  if (m_websocket) {
    auto handler = [self, completion_handler](const std::error_code& error_code, std::size_t bytes_transferred) {
      self->m_buffer_socket.set_size(bytes_transferred);
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
    rstream::webtty::protobuf::Message message;
    if (core::detail::parse_protobuf_message(message, m_buffer_socket.map().get_const_data(), m_buffer_socket.get_size())) {
      on_read_incoming_message(message);
    }
    else {
      on_error(error::code::protocol_error);
    }
  }
}

std::error_code server::impl::session::verify_client_proof(const rstream::webtty::protobuf::Open& open)
{
  if (!m_settings.m_require_client_proof) {
    return {};
  }
  if (!m_settings.m_endpoint_identity) {
    return error::code::protocol_error;
  }
  if (!open.has_client_proof()) {
    return error::code::client_proof_required;
  }
  const auto& proof       = open.client_proof();
  auto client_key_id      = bytes_from_string(proof.signing_key_id());
  m_client_signing_key_id = bytes_to_hex(client_key_id);
  m_client_principal_id   = proof.has_principal_id() ? string_from_value(proof.principal_id()) : std::string();
  m_client_device_id      = proof.has_device_id() ? string_from_value(proof.device_id()) : std::string();
  m_client_browser_id     = proof.has_browser_id() ? string_from_value(proof.browser_id()) : std::string();
  if (client_key_id.empty()) {
    return error::code::client_proof_invalid;
  }
  auto client_public_key = bytes_from_string(proof.signing_public_key());
  auto credential        = proof.has_credential() ? bytes_from_string(proof.credential().value()) : byte_vector{};
  auto it                = m_settings.m_authorized_client_signing_keys.find(string_from_bytes(client_key_id));
  byte_vector authorized_public_key;
  if (m_settings.m_client_proof_credential_verifier) {
    try {
      auto resolved = m_settings.m_client_proof_credential_verifier(client_key_id, client_public_key, credential);
      if (resolved) {
        authorized_public_key = *resolved;
      }
    }
    catch (...) {
      return error::code::client_unauthorized;
    }
  }
  if (authorized_public_key.empty() && it != m_settings.m_authorized_client_signing_keys.end()) {
    authorized_public_key = it->second;
  }
  else if (authorized_public_key.empty() && m_settings.m_authorized_client_signing_key_resolver) {
    try {
      auto resolved = m_settings.m_authorized_client_signing_key_resolver(client_key_id);
      if (resolved) {
        authorized_public_key = *resolved;
      }
    }
    catch (...) {
      return error::code::client_unauthorized;
    }
  }
  if (authorized_public_key.empty()) {
    return error::code::client_unauthorized;
  }
  if (authorized_public_key != client_public_key) {
    return error::code::client_unauthorized;
  }
  std::chrono::system_clock::time_point issued_at;
  std::chrono::system_clock::time_point expires_at;
  if (!parse_rfc3339_utc(issued_at, proof.issued_at()) || !parse_rfc3339_utc(expires_at, proof.expires_at())) {
    m_logger->debug("WebTTY client proof rejected: invalid proof time format [issued_at: {}, expires_at: {}]",
                    proof.issued_at(),
                    proof.expires_at());
    return error::code::client_proof_invalid;
  }
  auto now = std::chrono::system_clock::now();
  if (expires_at < now || issued_at > now + std::chrono::seconds(5) || expires_at - issued_at > std::chrono::seconds(30)) {
    m_logger->debug("WebTTY client proof rejected: proof time window is invalid [issued_at: {}, expires_at: {}]",
                    proof.issued_at(),
                    proof.expires_at());
    return error::code::client_proof_invalid;
  }
  rstream::webtty::protobuf::SessionKeyGrant grant;
  if (open.has_session_key_grant()) {
    grant = open.session_key_grant();
  }
  auto server_public = public_endpoint_identity(*m_settings.m_endpoint_identity);
  client_proof_transcript transcript;
  transcript.m_transport                = transport_string(m_protocol_type);
  transcript.m_workspace_id             = m_settings.m_workspace_id;
  transcript.m_project_id               = m_settings.m_project_id;
  transcript.m_server_id                = m_settings.m_server_id;
  transcript.m_session_id               = m_session_id;
  transcript.m_server_signing_key_id    = server_public.m_signing_key_id;
  transcript.m_server_encryption_key_id = server_public.m_encryption_key_id;
  transcript.m_server_nonce             = m_server_nonce;
  transcript.m_auth_requirement         = auth_requirement::client_proof;
  transcript.m_payload_suite            = payload_cipher_suite::aes_256_gcm;
  transcript.m_key_envelope_suite       = key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  if (!hash_proto_message(transcript.m_session_key_grant_hash, grant)) {
    m_logger->debug("WebTTY client proof rejected: failed to hash session key grant");
    return error::code::protocol_error;
  }
  if (open.has_config()) {
    if (!hash_proto_message(transcript.m_command_config_hash, open.config())) {
      m_logger->debug("WebTTY client proof rejected: failed to hash command config");
      return error::code::protocol_error;
    }
  }
  else {
    transcript.m_command_config_hash = sha256_bytes("");
  }
  transcript.m_client_principal_id    = string_from_value(proof.principal_id());
  transcript.m_client_signing_key_id  = client_key_id;
  transcript.m_client_credential_hash = proof.has_credential() ? sha256_bytes(proof.credential().value()) : sha256_bytes("");
  transcript.m_issued_at              = proof.issued_at();
  transcript.m_expires_at             = proof.expires_at();
  byte_vector expected_hash;
  std::error_code error_code;
  hash_webtty_client_proof_transcript(expected_hash, transcript, error_code);
  if (error_code) {
    return error_code;
  }
  if (expected_hash != bytes_from_string(proof.transcript_hash())) {
    m_logger->debug(
        "WebTTY client proof rejected: transcript hash mismatch [expected: {}, provided: {}, transport: {}, workspace_id: {}, project_id: {}, server_id: {}, session_id: {}, client_signing_key_id: {}, session_key_grant_hash: {}, command_config_hash: {}, client_credential_hash: {}]",
        bytes_to_hex(expected_hash),
        bytes_to_hex(bytes_from_string(proof.transcript_hash())),
        transcript.m_transport,
        transcript.m_workspace_id,
        transcript.m_project_id,
        transcript.m_server_id,
        transcript.m_session_id,
        bytes_to_hex(transcript.m_client_signing_key_id),
        bytes_to_hex(transcript.m_session_key_grant_hash),
        bytes_to_hex(transcript.m_command_config_hash),
        bytes_to_hex(transcript.m_client_credential_hash));
    return error::code::client_proof_invalid;
  }
  verify_webtty_client_proof_transcript(client_public_key, transcript, bytes_from_string(proof.signature()), error_code);
  if (error_code) {
    m_logger->debug("WebTTY client proof rejected: signature verification failed [client_signing_key_id: {}]", bytes_to_hex(transcript.m_client_signing_key_id));
    return error::code::client_proof_invalid;
  }
  return {};
}

void server::impl::session::on_read_incoming_message(const rstream::webtty::protobuf::Message& message)
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
  using payload_type = rstream::webtty::protobuf::Message::PayloadCase;
  auto message_type  = message.payload_case();
  if (message_type == payload_type::kAttach) {
    error_code = error::code::managed_attach_unsupported;
  }
  else if (!is_message_expected(m_state, message)) {
    error_code = error::code::unexpected_message;
  }
  else {
    if (message_type == payload_type::kOpen) {
      protocol::config protocol_config;
      detail::convert(protocol_config, message.open().config());
      error_code = verify_client_proof(message.open());
      if (!error_code) {
        if (message.open().has_session_key_grant()) {
          if (!m_settings.m_payload_crypto_resolver) {
            error_code = error::code::protocol_error;
          }
          else {
            session_key_grant grant;
            from_proto(grant, message.open().session_key_grant());
            m_payload_crypto = m_settings.m_payload_crypto_resolver->resolve(grant, error_code);
          }
        }
        else if (m_settings.m_payload_crypto_resolver) {
          error_code = error::code::e2e_session_key_grant_required;
        }
      }
      if (!error_code) {
        log_session_accepted();
        on_open(protocol_config);
      }
    }
    else if (message_type == payload_type::kError) {
      cancel_internal(error::code::client_error);
    }
    else if (message_type == payload_type::kData) {
      do_process_data(message.data());
    }
    else if (message_type == payload_type::kParameter) {
      const auto& parameter = message.parameter();
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
    do_send_error(error_code);
  }
  else if (do_read_messages) {
    do_read_incoming_message();
  }
}

void server::impl::session::do_process_data(const rstream::webtty::protobuf::Data& data)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_state == state::null || m_state == state::disconnected) {
    return;
  }
  if (data.type() != rstream::webtty::protobuf::Data::TYPE_STDIN) {
    do_send_error(error::code::unexpected_message);
  }
  else if (data.has_encrypted_data()) {
    if (!m_payload_crypto) {
      do_send_error(error::code::protocol_error);
      return;
    }
    std::error_code crypto_error;
    encrypted_payload encrypted;
    byte_vector plaintext;
    from_proto(encrypted, data.encrypted_data());
    m_payload_crypto->decrypt(payload_stream::std_in, encrypted, plaintext, crypto_error);
    if (crypto_error) {
      do_send_error(crypto_error);
      return;
    }
    auto buffer = std::make_shared<std::string>(plaintext.begin(), plaintext.end());
    do_process_data(buffer);
  }
  else if (m_payload_crypto && !data.has_eos()) {
    do_send_error(error::code::e2e_session_key_grant_required);
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
  m_child_done      = true;
  m_child_exit_code = code;
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

bool server::impl::session::is_message_expected(state state, const rstream::webtty::protobuf::Message& message)
{
  using payload_type                                                                               = rstream::webtty::protobuf::Message::PayloadCase;
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

}  // namespace webtty
}  // namespace rstream
