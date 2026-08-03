// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/socket.h>

#include <rstream/test/time.hpp>
#include <rstream/webtty/error.hpp>
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

static void set_receive_timeout(tcp::socket& socket)
{
  timeval timeout = {
      .tv_sec  = static_cast<time_t>(rstream::test::timeout(std::chrono::seconds(5)).count()),
      .tv_usec = 0,
  };
  auto rc = setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  assert(rc == 0);
}

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  const auto size          = static_cast<std::uint32_t>(message.ByteSizeLong());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  std::vector<char> payload(size);
  assert(message.SerializeToArray(payload.data(), static_cast<int>(payload.size())));
  if (!payload.empty()) {
    boost::asio::write(socket, boost::asio::buffer(payload));
  }
}

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

static protobuf::Message read_message(tcp::socket& socket)
{
  std::uint32_t frame_size = 0;
  read_exact(socket, &frame_size, sizeof(frame_size));
  const auto size = ntohl(frame_size);
  assert(size <= 1024 * 1024);
  std::vector<char> payload(size);
  if (!payload.empty()) {
    read_exact(socket, payload.data(), payload.size());
  }
  protobuf::Message message;
  const auto parsed = message.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
  assert(parsed);
  return message;
}

static bool read_error_or_eof(tcp::socket& socket)
{
  try {
    auto message = read_message(socket);
    return message.payload_case() == protobuf::Message::PayloadCase::kError || message.payload_case() == protobuf::Message::PayloadCase::kProtocolError;
  }
  catch (const boost::system::system_error& error) {
    return error.code() == boost::asio::error::eof;
  }
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

static protobuf::Message attach_message()
{
  protobuf::Message message;
  auto* attach = message.mutable_attach();
  attach->set_session_id("session-1");
  attach->set_participant_id("participant-1");
  attach->set_attach_grant("grant");
  attach->set_requested_role(protobuf::ATTACH_ROLE_SPECTATOR);
  attach->set_transport(protobuf::ATTACH_TRANSPORT_PLAIN);
  attach->add_capabilities(protobuf::ATTACH_CAPABILITY_READ_STREAM);
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

static protobuf::Message data_message(protobuf::Data::Type type, const std::string& data)
{
  protobuf::Message message;
  auto* payload = message.mutable_data();
  payload->set_type(type);
  payload->set_data(data);
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

static rstream::webtty::settings_server plain_server_settings(rstream::webtty::execution_mode execution_mode,
                                                              const rstream::webtty::payload_crypto_resolver::ptr& payload_crypto_resolver,
                                                              const boost::optional<rstream::webtty::endpoint_identity>& endpoint_identity,
                                                              const boost::optional<rstream::webtty::endpoint_identity>& client_identity,
                                                              const rstream::webtty::protocol::username& default_username,
                                                              bool allow_client_user,
                                                              const std::function<boost::optional<rstream::webtty::byte_vector>(const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&)>& credential_verifier = {})
{
  rstream::webtty::settings_server settings({
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = rstream::test::timeout_ms(5000),
              .m_close     = rstream::test::timeout_ms(5000),
              .m_heartbeat = 0,
          },
      },
      .m_timeouts_start_ms       = rstream::test::timeout_ms(5000),
      .m_std_out_buffer_size     = 64 * 1024,
      .m_std_err_buffer_size     = 64 * 1024,
      .m_execution_mode          = execution_mode,
      .m_default_username        = default_username,
      .m_allow_client_user       = allow_client_user,
      .m_payload_crypto_resolver = payload_crypto_resolver,
      .m_endpoint_identity       = endpoint_identity,
      .m_require_client_proof    = payload_crypto_resolver != nullptr,
  });
  if (client_identity) {
    settings.m_authorized_client_signing_keys[string_from_bytes(client_identity->m_signing.m_key_id)] = client_identity->m_signing.m_public_key;
  }
  if (credential_verifier) {
    settings.m_client_proof_credential_verifier = credential_verifier;
  }
  return settings;
}

class plain_webtty_server {
 public:
  explicit plain_webtty_server(rstream::webtty::execution_mode execution_mode                                                                                                                                                  = rstream::webtty::execution_mode::spawn,
                               rstream::webtty::payload_crypto_resolver::ptr payload_crypto_resolver                                                                                                                           = nullptr,
                               rstream::webtty::protocol::username default_username                                                                                                                                            = boost::none,
                               bool allow_client_user                                                                                                                                                                          = false,
                               boost::optional<rstream::webtty::endpoint_identity> endpoint_identity                                                                                                                           = boost::none,
                               boost::optional<rstream::webtty::endpoint_identity> client_identity                                                                                                                             = boost::none,
                               std::function<boost::optional<rstream::webtty::byte_vector>(const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&, const rstream::webtty::byte_vector&)> credential_verifier = {})
      : m_port(unused_tcp_port()),
        m_config({
            .m_address       = rstream::io::address(std::string("127.0.0.1:") + std::to_string(m_port)),
            .m_protocol_type = rstream::webtty::protocol::type::plain,
        }),
        m_settings(plain_server_settings(execution_mode, payload_crypto_resolver, endpoint_identity, client_identity, default_username, allow_client_user, credential_verifier)),
        m_server(std::make_shared<rstream::webtty::server>(m_io_context.get_executor(), m_config, m_settings))
  {
  }

  ~plain_webtty_server()
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

  std::error_code run_until_start_failure()
  {
    m_server->async_run([this](const std::error_code& error_code) {
      m_result = error_code;
      m_done   = true;
    });
    m_io_context.run();
    assert(m_done);
    assert(m_result);
    return m_result;
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

static tcp::socket connect_with_retry(boost::asio::io_context& io_context, unsigned short port)
{
  tcp::socket socket(io_context);
  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), port);
  const auto deadline = std::chrono::steady_clock::now() + rstream::test::timeout(std::chrono::seconds(5));
  boost::system::error_code error_code;
  do {
    socket.close();
    socket = tcp::socket(io_context);
    socket.connect(endpoint, error_code);
    if (!error_code) {
      set_receive_timeout(socket);
      return socket;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  } while (std::chrono::steady_clock::now() < deadline);
  std::cerr << "failed to connect to webtty server: " << error_code.message() << std::endl;
  assert(false);
  return socket;
}

static void check_invalid_command_returns_protocol_error_message(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({"definitely-not-a-rstream-test-command"}));
  auto message = read_message(socket);
  assert(message.payload_case() == protobuf::Message::PayloadCase::kError);
  assert(!message.error().msg().empty());
}

static void check_managed_attach_is_rejected(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, attach_message());
  auto message = read_message(socket);
  assert(message.payload_case() == protobuf::Message::PayloadCase::kError);
  assert(
      message.error().msg() == "managed WebTTY attach is handled by the rstream engine; direct WebTTY servers accept only new Open sessions");
}

static void check_plain_server_runs_child_and_streams_stdout_stderr(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({
                            "/bin/sh",
                            "-c",
                            "printf 'stdout-data'; printf 'stderr-data' >&2; exit 7",
                        }));

  bool saw_ack        = false;
  bool saw_stdout     = false;
  bool saw_stderr     = false;
  bool saw_stdout_eos = false;
  bool saw_stderr_eos = false;
  bool saw_close      = false;

  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kAck:
        saw_ack = true;
        break;
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT) {
          if (message.data().has_eos()) {
            saw_stdout_eos = true;
          }
          else if (message.data().data().find("stdout-data") != std::string::npos) {
            saw_stdout = true;
          }
        }
        else if (message.data().type() == protobuf::Data::TYPE_STDERR) {
          if (message.data().has_eos()) {
            saw_stderr_eos = true;
          }
          else if (message.data().data().find("stderr-data") != std::string::npos) {
            saw_stderr = true;
          }
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 7);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        std::cerr << "unexpected webtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_ack);
  assert(saw_stdout);
  assert(saw_stderr);
  assert(saw_stdout_eos);
  assert(saw_stderr_eos);
}

static void check_plain_server_applies_tty_environment_and_workdir(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket  = connect_with_retry(io_context, port);
  auto open    = open_message({
      "/bin/sh",
      "-c",
      "test -t 0 && test -t 1 && test \"$RSTREAM_CPP_WEBTTY_ENV\" = ok && test . -ef /tmp && printf runtime-ok",
  });
  auto* config = open.mutable_open()->mutable_config();
  config->mutable_options()->set_allocate_tty(true);
  config->mutable_workdir()->set_value("/tmp");
  auto* environment = config->add_env_vars();
  environment->set_key("RSTREAM_CPP_WEBTTY_ENV");
  environment->set_value("ok");
  write_message(socket, open);

  bool saw_ack     = false;
  bool saw_runtime = false;
  bool saw_close   = false;
  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kAck:
        saw_ack = true;
        break;
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT && message.data().data().find("runtime-ok") != std::string::npos) {
          saw_runtime = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 0);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        std::cerr << "unexpected webtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_ack);
  assert(saw_runtime);
}

static void check_plain_server_forwards_stdin_to_child_process(unsigned short port)
{
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, port);
  write_message(socket, open_message({"/bin/cat"}));

  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);

  write_message(socket, data_message(protobuf::Data::TYPE_STDIN, "stdin-through-webtty\n"));
  write_message(socket, eos_message(protobuf::Data::TYPE_STDIN));

  bool saw_stdout     = false;
  bool saw_stdout_eos = false;
  bool saw_stderr_eos = false;
  bool saw_close      = false;

  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT) {
          if (message.data().has_eos()) {
            saw_stdout_eos = true;
          }
          else if (message.data().data().find("stdin-through-webtty") != std::string::npos) {
            saw_stdout = true;
          }
        }
        else if (message.data().type() == protobuf::Data::TYPE_STDERR && message.data().has_eos()) {
          saw_stderr_eos = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 0);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        std::cerr << "unexpected webtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_stdout);
  assert(saw_stdout_eos);
  assert(saw_stderr_eos);
}

static void check_plain_server_e2e_forwards_stdin_to_child_process()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x31);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0001");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"plain\"}");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, client_identity);
  server.start();

  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(socket, authenticated_open_message({"/bin/cat"}, client_crypto, client_identity, hello.server_hello(), "plain"));

  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);

  rstream::webtty::encrypted_payload encrypted_stdin;
  client_crypto->encrypt(rstream::webtty::payload_stream::std_in, bytes_from_string("e2e-stdin-through-webtty\n"), encrypted_stdin, error_code);
  assert(!error_code);
  write_message(socket, encrypted_data_message(protobuf::Data::TYPE_STDIN, encrypted_stdin));
  write_message(socket, eos_message(protobuf::Data::TYPE_STDIN));

  bool saw_stdout     = false;
  bool saw_stdout_eos = false;
  bool saw_stderr_eos = false;
  bool saw_close      = false;

  while (!saw_close) {
    auto message = read_message(socket);
    switch (message.payload_case()) {
      case protobuf::Message::PayloadCase::kData:
        if (message.data().type() == protobuf::Data::TYPE_STDOUT) {
          if (message.data().has_eos()) {
            saw_stdout_eos = true;
          }
          else {
            assert(message.data().has_encrypted_data());
            rstream::webtty::encrypted_payload encrypted_stdout;
            rstream::webtty::byte_vector plaintext;
            from_proto(encrypted_stdout, message.data().encrypted_data());
            client_crypto->decrypt(rstream::webtty::payload_stream::std_out, encrypted_stdout, plaintext, error_code);
            assert(!error_code);
            if (string_from_bytes(plaintext).find("e2e-stdin-through-webtty") != std::string::npos) {
              saw_stdout = true;
            }
          }
        }
        else if (message.data().type() == protobuf::Data::TYPE_STDERR && message.data().has_eos()) {
          saw_stderr_eos = true;
        }
        break;
      case protobuf::Message::PayloadCase::kClose:
        assert(message.close().return_code() == 0);
        saw_close = true;
        break;
      case protobuf::Message::PayloadCase::kHeartbeat:
        break;
      default:
        std::cerr << "unexpected webtty message type: " << message.payload_case() << std::endl;
        assert(false);
        break;
    }
  }

  assert(saw_stdout);
  assert(saw_stdout_eos);
  assert(saw_stderr_eos);
}

static void check_plain_server_e2e_accepts_client_credential_verifier()
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
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x41);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0041");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"plain\"}");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  plain_webtty_server server(rstream::webtty::execution_mode::spawn,
                             rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption),
                             boost::none,
                             false,
                             server_identity,
                             boost::none,
                             verifier);
  server.start();

  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(socket, authenticated_open_message({"/bin/cat"}, client_crypto, client_identity, hello.server_hello(), "plain", credential));
  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);
  write_message(socket, eos_message(protobuf::Data::TYPE_STDIN));
  bool saw_close = false;
  while (!saw_close) {
    auto message = read_message(socket);
    if (message.payload_case() == protobuf::Message::PayloadCase::kClose) {
      saw_close = true;
    }
  }
}

static void check_plain_server_e2e_rejects_missing_session_key_grant()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  auto open = open_message({"/bin/cat"});
  add_client_proof(*open.mutable_open(), hello.server_hello(), client_identity, "plain");
  write_message(socket, open);
  const auto rejected = read_error_or_eof(socket);
  assert(rejected);
}

static void check_plain_server_e2e_rejects_missing_client_proof()
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
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(socket, open_message({"/bin/cat"}, client_crypto));
  const auto rejected = read_error_or_eof(socket);
  assert(rejected);
}

static void check_plain_server_e2e_rejects_expired_client_proof()
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
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  auto open = open_message({"/bin/cat"}, client_crypto);
  add_client_proof(*open.mutable_open(), hello.server_hello(), client_identity, "plain", -60, -30);
  write_message(socket, open);
  const auto rejected = read_error_or_eof(socket);
  assert(rejected);
}

static void check_plain_server_e2e_rejects_unauthorized_client_proof()
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
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, authorized_client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  auto open = open_message({"/bin/cat"}, client_crypto);
  add_client_proof(*open.mutable_open(), hello.server_hello(), denied_client_identity, "plain");
  write_message(socket, open);
  const auto rejected = read_error_or_eof(socket);
  assert(rejected);
}

static void check_plain_server_e2e_rejects_plaintext_data()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity server_identity;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(server_identity, error_code);
  assert(!error_code);
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x32);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0002");
  crypto_config.m_recipients     = {{server_identity.m_encryption.m_key_id, server_identity.m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  plain_webtty_server server(rstream::webtty::execution_mode::spawn, rstream::webtty::make_e2e_server_payload_crypto_resolver(server_identity.m_encryption), boost::none, false, server_identity, client_identity);
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  auto hello  = read_message(socket);
  assert(hello.payload_case() == protobuf::Message::PayloadCase::kServerHello);
  write_message(socket, authenticated_open_message({"/bin/cat"}, client_crypto, client_identity, hello.server_hello(), "plain"));
  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);
  write_message(socket, data_message(protobuf::Data::TYPE_STDIN, "plaintext\n"));
  const auto rejected = read_error_or_eof(socket);
  assert(rejected);
}

static void check_login_execution_mode_requires_user()
{
  plain_webtty_server server(rstream::webtty::execution_mode::login);
  const auto error_code = server.run_until_start_failure();
  assert(error_code == rstream::webtty::error::make_error_code(rstream::webtty::error::code::login_user_required));
}

static void check_login_execution_mode_rejects_unknown_user_before_listen()
{
  plain_webtty_server server(rstream::webtty::execution_mode::login, nullptr, rstream::webtty::protocol::identifier("rstream-webtty-user-that-must-not-exist"));
  const auto error_code = server.run_until_start_failure();
  assert(error_code);
  assert(error_code != rstream::webtty::error::make_error_code(rstream::webtty::error::code::login_user_required));
}

static void check_login_execution_mode_runs_as_configured_user()
{
  std::error_code error_code;
  rstream::webtty::protocol::user_info user_info;
  rstream::webtty::protocol::get_user_info(user_info, boost::none, error_code);
  assert(!error_code);
  plain_webtty_server server(rstream::webtty::execution_mode::login, nullptr, rstream::webtty::protocol::identifier(user_info.m_name));
  server.start();
  boost::asio::io_context io_context;
  auto socket = connect_with_retry(io_context, server.port());
  write_message(socket, open_message({"/bin/sh", "-c", "printf '%s' \"$USER|$LOGNAME|$HOME|$SHELL\""}));
  auto ack = read_message(socket);
  assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);
  auto out = read_message(socket);
  assert(out.payload_case() == protobuf::Message::PayloadCase::kData);
  assert(out.data().type() == protobuf::Data::TYPE_STDOUT);
  assert(out.data().data() == user_info.m_name + "|" + user_info.m_name + "|" + user_info.m_home + "|" + user_info.m_shell);
}

static void check_plain_server_cancel_keeps_active_child_resources_alive()
{
  for (std::size_t iteration = 0; iteration < 16; ++iteration) {
    plain_webtty_server server;
    server.start();
    boost::asio::io_context io_context;
    auto socket = connect_with_retry(io_context, server.port());
    write_message(socket, open_message({"/bin/sh", "-c", "printf active; exec sleep 30"}));
    auto ack = read_message(socket);
    assert(ack.payload_case() == protobuf::Message::PayloadCase::kAck);
    auto out = read_message(socket);
    assert(out.payload_case() == protobuf::Message::PayloadCase::kData);
    assert(out.data().type() == protobuf::Data::TYPE_STDOUT);
    assert(out.data().data() == "active");
    server.stop();
    boost::system::error_code ignored;
    socket.close(ignored);
  }
}

template <typename check_type>
static void run_check(const char* name, check_type&& check)
{
  try {
    std::forward<check_type>(check)();
  }
  catch (const std::exception& error) {
    std::cerr << name << " failed: " << error.what() << std::endl;
    throw;
  }
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  plain_webtty_server server;
  server.start();
  run_check("invalid command", [&server] { check_invalid_command_returns_protocol_error_message(server.port()); });
  run_check("managed attach rejection", [&server] { check_managed_attach_is_rejected(server.port()); });
  run_check("stdout and stderr streaming", [&server] { check_plain_server_runs_child_and_streams_stdout_stderr(server.port()); });
  run_check("tty environment and workdir", [&server] { check_plain_server_applies_tty_environment_and_workdir(server.port()); });
  run_check("stdin forwarding", [&server] { check_plain_server_forwards_stdin_to_child_process(server.port()); });
  run_check("E2E stdin forwarding", check_plain_server_e2e_forwards_stdin_to_child_process);
  run_check("client credential verification", check_plain_server_e2e_accepts_client_credential_verifier);
  run_check("missing session key rejection", check_plain_server_e2e_rejects_missing_session_key_grant);
  run_check("missing client proof rejection", check_plain_server_e2e_rejects_missing_client_proof);
  run_check("expired client proof rejection", check_plain_server_e2e_rejects_expired_client_proof);
  run_check("unauthorized client proof rejection", check_plain_server_e2e_rejects_unauthorized_client_proof);
  run_check("plaintext rejection", check_plain_server_e2e_rejects_plaintext_data);
  run_check("login mode user requirement", check_login_execution_mode_requires_user);
  run_check("login mode rejects unknown user before listen", check_login_execution_mode_rejects_unknown_user_before_listen);
  run_check("login mode configured user", check_login_execution_mode_runs_as_configured_user);
  run_check("cancellation with active child", check_plain_server_cancel_keeps_active_child_resources_alive);
  return 0;
}
