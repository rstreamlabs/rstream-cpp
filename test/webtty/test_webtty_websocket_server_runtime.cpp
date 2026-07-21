// See LICENSE file in the project root for license information.

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <google/protobuf/wrappers.pb.h>
#include <openssl/sha.h>
#include <spdlog/sinks/sink.h>

#include <rstream/core/log.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/server.hpp>
#include <rstream/webtty/webtty.hpp>

namespace protobuf = rstream::webtty::protobuf;
using tcp          = boost::asio::ip::tcp;

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static protobuf::Message open_message(const std::vector<std::string>& cmd_args)
{
  protobuf::Message message;
  auto* config = message.mutable_open()->mutable_config();
  config->mutable_options()->set_interactive(false);
  config->mutable_options()->set_allocate_tty(false);
  config->mutable_options()->set_send_heartbeat(false);
  for (const auto& arg : cmd_args) {
    config->add_cmd_args(arg);
  }
  return message;
}

static rstream::webtty::byte_vector bytes_from_string(const std::string& value)
{
  return rstream::webtty::byte_vector(value.begin(), value.end());
}

static std::string string_from_bytes(const rstream::webtty::byte_vector& value)
{
  return std::string(value.begin(), value.end());
}

static std::string string_from_value(const google::protobuf::StringValue& value)
{
  return value.value();
}

class capture_log_sink : public spdlog::sinks::sink {
 public:
  std::vector<std::string> messages() const
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_messages;
  }

 private:
  void log(const spdlog::details::log_msg& msg) override
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_messages.push_back(std::string(msg.payload.begin(), msg.payload.end()));
  }

  void flush() override {}

  void set_pattern(const std::string& pattern) override
  {
    (void)pattern;
  }

  void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override
  {
    (void)sink_formatter;
  }

  mutable std::mutex m_mutex;
  std::vector<std::string> m_messages;
};

static std::optional<std::string> find_log_message(const std::shared_ptr<capture_log_sink>& sink, const std::string& needle)
{
  for (const auto& message : sink->messages()) {
    if (message.find(needle) != std::string::npos) {
      return message;
    }
  }
  return std::nullopt;
}

static void assert_log_contains(const std::string& message, const std::string& needle)
{
  if (message.find(needle) == std::string::npos) {
    std::cerr << "missing log field '" << needle << "' in: " << message << std::endl;
    assert(false);
  }
}

static void to_proto(protobuf::KeyEnvelope& dst, const rstream::webtty::key_envelope& src)
{
  dst.set_recipient_key_id(string_from_bytes(src.m_recipient_key_id));
  dst.set_encapsulated_key(string_from_bytes(src.m_encapsulated_key));
  dst.set_wrapped_key(string_from_bytes(src.m_wrapped_key));
}

static void to_proto(protobuf::SessionKeyGrant& dst, const rstream::webtty::session_key_grant& src)
{
  dst.set_payload_suite(static_cast<protobuf::PayloadCipherSuite>(static_cast<int>(src.m_payload_suite)));
  dst.set_payload_key_id(string_from_bytes(src.m_payload_key_id));
  for (const auto& envelope : src.m_key_envelopes) {
    to_proto(*dst.add_key_envelopes(), envelope);
  }
  dst.set_key_context(string_from_bytes(src.m_key_context));
  dst.set_key_envelope_suite(static_cast<protobuf::KeyEnvelopeSuite>(static_cast<int>(src.m_key_envelope_suite)));
}

static void to_proto(protobuf::PayloadCrypto& dst, const rstream::webtty::payload_crypto_metadata& src)
{
  dst.set_payload_suite(static_cast<protobuf::PayloadCipherSuite>(static_cast<int>(src.m_payload_suite)));
  dst.set_payload_key_id(string_from_bytes(src.m_payload_key_id));
  dst.set_nonce(string_from_bytes(src.m_nonce));
  dst.set_aad_context(string_from_bytes(src.m_aad_context));
}

static void to_proto(protobuf::EncryptedPayload& dst, const rstream::webtty::encrypted_payload& src)
{
  dst.set_ciphertext(string_from_bytes(src.m_ciphertext));
  dst.set_plaintext_length(src.m_plaintext_length);
  to_proto(*dst.mutable_payload_crypto(), src.m_payload_crypto);
}

static void from_proto(rstream::webtty::encrypted_payload& dst, const protobuf::EncryptedPayload& src)
{
  dst.m_ciphertext       = bytes_from_string(src.ciphertext());
  dst.m_plaintext_length = src.plaintext_length();
  if (src.has_payload_crypto()) {
    dst.m_payload_crypto.m_payload_suite  = static_cast<rstream::webtty::payload_cipher_suite>(src.payload_crypto().payload_suite());
    dst.m_payload_crypto.m_payload_key_id = bytes_from_string(src.payload_crypto().payload_key_id());
    dst.m_payload_crypto.m_nonce          = bytes_from_string(src.payload_crypto().nonce());
    dst.m_payload_crypto.m_aad_context    = bytes_from_string(src.payload_crypto().aad_context());
  }
}

static protobuf::Message open_message(const std::vector<std::string>& cmd_args, const rstream::webtty::payload_crypto::ptr& payload_crypto)
{
  auto message = open_message(cmd_args);
  if (payload_crypto) {
    std::error_code error_code;
    rstream::webtty::session_key_grant session_key_grant;
    payload_crypto->get_session_key_grant(session_key_grant, error_code);
    assert(!error_code);
    message.mutable_open()->add_capabilities(protobuf::OPEN_CAPABILITY_ENCRYPTED_PAYLOAD);
    message.mutable_open()->add_capabilities(protobuf::OPEN_CAPABILITY_SESSION_CRYPTO);
    to_proto(*message.mutable_open()->mutable_session_key_grant(), session_key_grant);
  }
  return message;
}

static protobuf::Message encrypted_data_message(protobuf::Data::Type type, const rstream::webtty::encrypted_payload& encrypted)
{
  protobuf::Message message;
  auto* payload = message.mutable_data();
  payload->set_type(type);
  to_proto(*payload->mutable_encrypted_data(), encrypted);
  return message;
}

static protobuf::Message eos_message(protobuf::Data::Type type)
{
  protobuf::Message message;
  auto* payload = message.mutable_data();
  payload->set_type(type);
  payload->mutable_eos();
  return message;
}

static rstream::webtty::byte_vector sha256_message(const google::protobuf::Message& message)
{
  std::string bytes;
  const auto serialized = message.SerializeToString(&bytes);
  assert(serialized);
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), digest);
  return rstream::webtty::byte_vector(digest, digest + SHA256_DIGEST_LENGTH);
}

static rstream::webtty::byte_vector sha256_bytes(const std::string& value)
{
  unsigned char digest[SHA256_DIGEST_LENGTH] = {};
  SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
  return rstream::webtty::byte_vector(digest, digest + SHA256_DIGEST_LENGTH);
}

static std::string rfc3339(std::chrono::system_clock::time_point value)
{
  auto time = std::chrono::system_clock::to_time_t(value);
  std::tm tm{};
  gmtime_r(&time, &tm);
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

static void add_client_proof(protobuf::Open& open, const protobuf::ServerHello& hello, const rstream::webtty::endpoint_identity& client_identity, const std::string& transport, int issued_at_offset_seconds = 0, int expires_at_offset_seconds = 30, const std::string& credential = "")
{
  auto now        = std::chrono::system_clock::now();
  auto issued_at  = rfc3339(now + std::chrono::seconds(issued_at_offset_seconds));
  auto expires_at = rfc3339(now + std::chrono::seconds(expires_at_offset_seconds));
  rstream::webtty::client_proof_transcript transcript;
  transcript.m_transport                = transport;
  transcript.m_workspace_id             = string_from_value(hello.workspace_id());
  transcript.m_project_id               = string_from_value(hello.project_id());
  transcript.m_server_id                = string_from_value(hello.server_id());
  transcript.m_session_id               = hello.session_id();
  transcript.m_server_signing_key_id    = bytes_from_string(hello.server_identity().signing_key_id());
  transcript.m_server_encryption_key_id = bytes_from_string(hello.server_identity().encryption_key_id());
  transcript.m_server_nonce             = bytes_from_string(hello.session_nonce());
  transcript.m_auth_requirement         = rstream::webtty::auth_requirement::client_proof;
  transcript.m_payload_suite            = rstream::webtty::payload_cipher_suite::aes_256_gcm;
  transcript.m_key_envelope_suite       = rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  transcript.m_session_key_grant_hash   = sha256_message(open.session_key_grant());
  transcript.m_command_config_hash      = sha256_message(open.config());
  transcript.m_client_signing_key_id    = client_identity.m_signing.m_key_id;
  transcript.m_client_credential_hash   = credential.empty() ? sha256_bytes("") : sha256_bytes(credential);
  transcript.m_issued_at                = issued_at;
  transcript.m_expires_at               = expires_at;
  rstream::webtty::byte_vector transcript_hash;
  rstream::webtty::byte_vector signature;
  std::error_code error_code;
  rstream::webtty::sign_webtty_client_proof_transcript(transcript_hash, signature, client_identity.m_signing, transcript, error_code);
  assert(!error_code);
  auto proof = open.mutable_client_proof();
  proof->set_signing_key_id(string_from_bytes(client_identity.m_signing.m_key_id));
  proof->set_signing_public_key(string_from_bytes(client_identity.m_signing.m_public_key));
  proof->set_signature_suite(protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  proof->set_transcript_hash(string_from_bytes(transcript_hash));
  proof->set_signature(string_from_bytes(signature));
  proof->set_issued_at(issued_at);
  proof->set_expires_at(expires_at);
  if (!credential.empty()) {
    proof->mutable_credential()->set_value(credential);
  }
}

static protobuf::Message authenticated_open_message(const std::vector<std::string>& cmd_args,
                                                    const rstream::webtty::payload_crypto::ptr& payload_crypto,
                                                    const rstream::webtty::endpoint_identity& client_identity,
                                                    const protobuf::ServerHello& hello,
                                                    const std::string& transport,
                                                    const std::string& credential = "")
{
  auto message = open_message(cmd_args, payload_crypto);
  add_client_proof(*message.mutable_open(), hello, client_identity, transport, 0, 30, credential);
  return message;
}

static rstream::webtty::settings_server websocket_server_settings(const rstream::webtty::payload_crypto_resolver::ptr& payload_crypto_resolver,
                                                                  const boost::optional<rstream::webtty::endpoint_identity>& endpoint_identity,
                                                                  const boost::optional<rstream::webtty::endpoint_identity>& client_identity,
                                                                  const std::function<boost::optional<rstream::webtty::byte_vector>(const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&)>& credential_verifier = {},
                                                                  const std::string& workspace_id                                                                                                                                                                        = "",
                                                                  const std::string& project_id                                                                                                                                                                          = "",
                                                                  const std::string& server_id                                                                                                                                                                           = "")
{
  rstream::webtty::settings_server settings({
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 0,
          },
      },
      .m_timeouts_start_ms       = 5000,
      .m_std_out_buffer_size     = 64 * 1024,
      .m_std_err_buffer_size     = 64 * 1024,
      .m_payload_crypto_resolver = payload_crypto_resolver,
      .m_endpoint_identity       = endpoint_identity,
      .m_require_client_proof    = payload_crypto_resolver != nullptr,
      .m_workspace_id            = workspace_id,
      .m_project_id              = project_id,
      .m_server_id               = server_id,
  });
  if (client_identity) {
    settings.m_authorized_client_signing_keys[string_from_bytes(client_identity->m_signing.m_key_id)] = client_identity->m_signing.m_public_key;
  }
  if (credential_verifier) {
    settings.m_client_proof_credential_verifier = credential_verifier;
  }
  return settings;
}

class websocket_webtty_server {
 public:
  explicit websocket_webtty_server(rstream::webtty::payload_crypto_resolver::ptr payload_crypto_resolver                                                                                                                           = nullptr,
                                   boost::optional<rstream::webtty::endpoint_identity> endpoint_identity                                                                                                                           = boost::none,
                                   boost::optional<rstream::webtty::endpoint_identity> client_identity                                                                                                                             = boost::none,
                                   std::function<boost::optional<rstream::webtty::byte_vector>(const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&)> credential_verifier = {},
                                   std::string workspace_id                                                                                                                                                                        = "",
                                   std::string project_id                                                                                                                                                                          = "",
                                   std::string server_id                                                                                                                                                                           = "")
      : m_port(unused_tcp_port()),
        m_config({
            .m_address       = rstream::io::address(std::string("127.0.0.1:") + std::to_string(m_port)),
            .m_protocol_type = rstream::webtty::protocol::type::websocket,
        }),
        m_settings(websocket_server_settings(payload_crypto_resolver, endpoint_identity, client_identity, credential_verifier, workspace_id, project_id, server_id)),
        m_server(std::make_shared<rstream::webtty::server>(m_io_context.get_executor(), m_config, m_settings))
  {
  }

  ~websocket_webtty_server()
  {
    stop();
  }

  unsigned short port() const
  {
    return m_port;
  }

  void start()
  {
    m_server->async_run([this](const std::error_code& error_code) {
      m_result = error_code;
      m_done   = true;
    });
    m_thread = std::thread([this] {
      try {
        m_io_context.run();
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void stop()
  {
    if (!m_thread.joinable()) {
      return;
    }
    m_server->cancel();
    m_thread.join();
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
    assert(m_done);
    assert(!m_result);
  }

 private:
  unsigned short m_port;
  rstream::webtty::server::config m_config;
  rstream::webtty::settings_server m_settings;
  boost::asio::io_context m_io_context;
  std::shared_ptr<rstream::webtty::server> m_server;
  std::thread m_thread;
  std::exception_ptr m_exception;
  bool m_done = false;
  std::error_code m_result;
};

static void read_exact(tcp::socket& socket, void* data, std::size_t size)
{
  std::size_t offset = 0;
  while (offset < size) {
    boost::system::error_code error_code;
    auto bytes = socket.read_some(boost::asio::buffer(static_cast<char*>(data) + offset, size - offset), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read");
    }
    offset += bytes;
  }
}

static void write_all(tcp::socket& socket, const void* data, std::size_t size)
{
  boost::asio::write(socket, boost::asio::buffer(data, size));
}

static void websocket_handshake(tcp::socket& socket)
{
  const std::string request =
      "GET /api/websocket HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Key: AAAAAAAAAAAAAAAAAAAAAA==\r\n"
      "Sec-WebSocket-Version: 13\r\n"
      "\r\n";
  boost::asio::write(socket, boost::asio::buffer(request));

  std::string response;
  while (response.find("\r\n\r\n") == std::string::npos) {
    boost::system::error_code error_code;
    char byte  = 0;
    auto bytes = socket.read_some(boost::asio::buffer(&byte, 1), error_code);
    if (error_code == boost::asio::error::interrupted) {
      continue;
    }
    if (error_code) {
      throw boost::system::system_error(error_code, "read websocket handshake");
    }
    response.append(&byte, bytes);
    assert(response.size() <= 8192);
  }
  assert(response.find(" 101 ") != std::string::npos);
}

static tcp::socket connect_with_retry(boost::asio::io_context& io_context, unsigned short port)
{
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  boost::system::error_code error_code;
  do {
    tcp::socket socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      try {
        websocket_handshake(socket);
        return socket;
      }
      catch (const boost::system::system_error& error) {
        error_code = error.code();
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect websocket webtty client: " << error_code.message() << std::endl;
  assert(false);
  return tcp::socket(io_context);
}

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  std::vector<char> payload(message.ByteSizeLong());
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));

  std::vector<unsigned char> frame;
  frame.push_back(0x82);
  if (payload.size() < 126) {
    frame.push_back(static_cast<unsigned char>(0x80 | payload.size()));
  }
  else {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xff));
    frame.push_back(static_cast<unsigned char>(payload.size() & 0xff));
  }
  const std::array<unsigned char, 4> mask = {0x11, 0x22, 0x33, 0x44};
  frame.insert(frame.end(), mask.begin(), mask.end());
  for (std::size_t i = 0; i < payload.size(); ++i) {
    frame.push_back(static_cast<unsigned char>(payload[i]) ^ mask[i % mask.size()]);
  }
  write_all(socket, frame.data(), frame.size());
}

static std::optional<protobuf::Message> read_message(tcp::socket& socket)
{
  while (true) {
    std::array<unsigned char, 2> header{};
    read_exact(socket, header.data(), header.size());
    const auto opcode  = header[0] & 0x0f;
    bool masked        = (header[1] & 0x80) != 0;
    std::uint64_t size = header[1] & 0x7f;
    if (size == 126) {
      std::array<unsigned char, 2> extended{};
      read_exact(socket, extended.data(), extended.size());
      size = (static_cast<std::uint64_t>(extended[0]) << 8) | extended[1];
    }
    else if (size == 127) {
      std::array<unsigned char, 8> extended{};
      read_exact(socket, extended.data(), extended.size());
      size = 0;
      for (auto byte : extended) {
        size = (size << 8) | byte;
      }
    }
    std::array<unsigned char, 4> mask{};
    if (masked) {
      read_exact(socket, mask.data(), mask.size());
    }
    assert(size <= 1024 * 1024);
    std::vector<char> payload(size);
    if (!payload.empty()) {
      read_exact(socket, payload.data(), payload.size());
    }
    if (masked) {
      for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % mask.size()]);
      }
    }
    if (opcode == 0x8) {
      return std::nullopt;
    }
    if (opcode != 0x2) {
      continue;
    }
    protobuf::Message message;
    const auto parsed = message.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
    assert(parsed);
    return message;
  }
}

static bool read_error_or_close(tcp::socket& socket)
{
  try {
    auto message = read_message(socket);
    return !message || message->payload_case() == protobuf::Message::PayloadCase::kError || message->payload_case() == protobuf::Message::PayloadCase::kProtocolError;
  }
  catch (const boost::system::system_error& error) {
    return error.code() == boost::asio::error::eof || error.code() == boost::asio::error::connection_reset;
  }
}

static void check_websocket_server_runs_child_and_closes_normally(unsigned short port)
{
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, port);
  write_message(websocket, open_message({
                               "/bin/sh",
                               "-c",
                               "printf 'websocket-stdout'; exit 3",
                           }));

  bool saw_ack    = false;
  bool saw_stdout = false;
  bool saw_close  = false;
  while (!saw_close) {
    auto maybe_message = read_message(websocket);
    if (!maybe_message) {
      std::cerr << "websocket closed before protocol close; saw_ack=" << saw_ack << " saw_stdout=" << saw_stdout << std::endl;
      assert(maybe_message);
    }
    const auto& message = maybe_message.value();
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kAck:
        saw_ack = true;
        break;
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT && !message.data().has_eos() && message.data().data().find("websocket-stdout") != std::string::npos) {
          saw_stdout = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 3);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        std::cerr << "unexpected websocket webtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }
  assert(saw_ack);
  assert(saw_stdout);
}

static void check_websocket_server_e2e_forwards_stdin_and_closes_normally()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x33);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0003");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"websocket\"}");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);

  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(websocket, authenticated_open_message({"/bin/cat"}, client_crypto, client_identity, hello->server_hello(), "websocket"));

  auto ack = read_message(websocket);
  assert(ack);
  assert(ack->payload_case() == protobuf::Message::PayloadCase::kAck);

  rstream::webtty::encrypted_payload encrypted_stdin;
  client_crypto->encrypt(rstream::webtty::payload_stream::std_in, bytes_from_string("websocket-e2e-stdin\n"), encrypted_stdin, error_code);
  assert(!error_code);
  write_message(websocket, encrypted_data_message(protobuf::Data::TYPE_STDIN, encrypted_stdin));
  write_message(websocket, eos_message(protobuf::Data::TYPE_STDIN));

  bool saw_stdout = false;
  bool saw_close  = false;
  while (!saw_close) {
    auto maybe_message = read_message(websocket);
    assert(maybe_message);
    const auto& message = maybe_message.value();
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT && !message.data().has_eos()) {
          assert(message.data().has_encrypted_data());
          rstream::webtty::encrypted_payload encrypted_stdout;
          rstream::webtty::byte_vector plaintext;
          from_proto(encrypted_stdout, message.data().encrypted_data());
          client_crypto->decrypt(rstream::webtty::payload_stream::std_out, encrypted_stdout, plaintext, error_code);
          assert(!error_code);
          if (string_from_bytes(plaintext).find("websocket-e2e-stdin") != std::string::npos) {
            saw_stdout = true;
          }
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 0);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        break;
    }
  }
  assert(saw_stdout);
}

static void check_websocket_server_e2e_accepts_client_credential_verifier()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  const auto credential = std::string("{\"type\":\"test.workspace.credential\",\"v\":1}");
  auto verifier         = [client_identity, credential](const rstream::webtty::byte_vector& client_key_id,
                                                const rstream::webtty::byte_vector& client_public_key,
                                                const rstream::webtty::byte_vector& client_credential) -> boost::optional<rstream::webtty::byte_vector> {
    assert(client_key_id == client_identity.m_signing.m_key_id);
    assert(client_public_key == client_identity.m_signing.m_public_key);
    assert(string_from_bytes(client_credential) == credential);
    return client_public_key;
  };
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x42);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0042");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"websocket\"}");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);

  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), server_identity, boost::none, verifier);
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(websocket, authenticated_open_message({"/bin/cat"}, client_crypto, client_identity, hello->server_hello(), "websocket", credential));
  auto ack = read_message(websocket);
  assert(ack);
  assert(ack->payload_case() == protobuf::Message::PayloadCase::kAck);
  write_message(websocket, eos_message(protobuf::Data::TYPE_STDIN));
  bool saw_close = false;
  while (!saw_close) {
    auto maybe_message = read_message(websocket);
    assert(maybe_message);
    if (maybe_message->payload_case() == protobuf::Message::PayloadCase::kClose) {
      saw_close = true;
    }
  }
}

static void check_websocket_server_logs_audit_fields_for_authenticated_e2e_session()
{
  auto sink = std::make_shared<capture_log_sink>();
  rstream::core::log::subscribe(sink);

  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x43);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0043");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"websocket\"}");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);

  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption),
                                 server_identity,
                                 client_identity,
                                 {},
                                 "workspace-log-test",
                                 "project-log-test",
                                 "server-log-test");
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(websocket, authenticated_open_message({"/bin/sh", "-c", "printf log-audit-ok"}, client_crypto, client_identity, hello->server_hello(), "websocket"));

  bool saw_close = false;
  while (!saw_close) {
    auto maybe_message = read_message(websocket);
    if (!maybe_message) {
      break;
    }
    if (maybe_message->payload_case() == protobuf::Message::PayloadCase::kClose) {
      saw_close = true;
    }
  }
  websocket.close();
  server.stop();

  const auto accepted = find_log_message(sink, "session accepted");
  assert(accepted);
  for (const auto& field : {
           "workspace_id: workspace-log-test",
           "project_id: project-log-test",
           "server_id: server-log-test",
           "server_signing_key_id:",
           "client_signing_key_id:",
           "e2e: true",
           "client_proof_required: true",
           "execution_mode: spawn",
       }) {
    assert_log_contains(*accepted, field);
  }

  const auto closed = find_log_message(sink, "session closed");
  assert(closed);
  for (const auto& field : {
           "workspace_id: workspace-log-test",
           "project_id: project-log-test",
           "server_id: server-log-test",
           "server_signing_key_id:",
           "client_signing_key_id:",
           "opened: true",
           "authenticated: true",
           "e2e: true",
       }) {
    assert_log_contains(*closed, field);
  }
}

static void check_websocket_server_e2e_rejects_missing_client_proof()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x34);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0004");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(websocket, open_message({"/bin/cat"}, client_crypto));
  const auto rejected = read_error_or_close(websocket);
  assert(rejected);
}

static void check_websocket_server_e2e_rejects_expired_client_proof()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x35);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0005");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  auto open = open_message({"/bin/cat"}, client_crypto);
  add_client_proof(*open.mutable_open(), hello->server_hello(), client_identity, "websocket", -60, -30);
  write_message(websocket, open);
  const auto rejected = read_error_or_close(websocket);
  assert(rejected);
}

static void check_websocket_server_e2e_rejects_unauthorized_client_proof()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity authorized_client_identity;
  rstream::webtty::endpoint_identity denied_client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(authorized_client_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(denied_client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x36);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0006");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  websocket_webtty_server server(rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), server_identity, authorized_client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto websocket = connect_with_retry(io_context, server.port());
  auto hello     = read_message(websocket);
  assert(hello);
  assert(hello->payload_case() == protobuf::Message::PayloadCase::kServerHello);
  auto open = open_message({"/bin/cat"}, client_crypto);
  add_client_proof(*open.mutable_open(), hello->server_hello(), denied_client_identity, "websocket");
  write_message(websocket, open);
  const auto rejected = read_error_or_close(websocket);
  assert(rejected);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  websocket_webtty_server server;
  server.start();
  check_websocket_server_runs_child_and_closes_normally(server.port());
  check_websocket_server_e2e_forwards_stdin_and_closes_normally();
  check_websocket_server_e2e_accepts_client_credential_verifier();
  check_websocket_server_logs_audit_fields_for_authenticated_e2e_session();
  check_websocket_server_e2e_rejects_missing_client_proof();
  check_websocket_server_e2e_rejects_expired_client_proof();
  check_websocket_server_e2e_rejects_unauthorized_client_proof();
  return 0;
}
