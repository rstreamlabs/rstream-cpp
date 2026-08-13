// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http/read.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/websocket.hpp>

#include <fcntl.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <rstream/test/time.hpp>
#include <rstream/webtty/client.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/webtty.hpp>

namespace http      = boost::beast::http;
namespace websocket = boost::beast::websocket;
namespace protobuf  = rstream::webtty::protobuf;
using tcp           = boost::asio::ip::tcp;

static void require_posix(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class fd_guard {
 public:
  explicit fd_guard(int fd = -1)
      : m_fd(fd)
  {
  }

  ~fd_guard()
  {
    reset();
  }

  int get() const
  {
    return m_fd;
  }

  void reset(int fd = -1)
  {
    if (m_fd != -1) {
      close(m_fd);
    }
    m_fd = fd;
  }

 private:
  int m_fd;
};

class stdin_data {
 public:
  explicit stdin_data(const std::string& data, bool keep_open = false)
  {
    int fds[2] = {-1, -1};
    require_posix(pipe(fds) == 0, "pipe failed");
    m_read.reset(fds[0]);
    m_write.reset(fds[1]);
    std::size_t offset = 0;
    while (offset < data.size()) {
      auto n = write(m_write.get(), data.data() + offset, data.size() - offset);
      require_posix(n > 0, "write failed");
      offset += static_cast<std::size_t>(n);
    }
    if (!keep_open) {
      m_write.reset();
    }
    m_saved.reset(dup(STDIN_FILENO));
    require_posix(m_saved.get() != -1, "dup failed");
    require_posix(dup2(m_read.get(), STDIN_FILENO) != -1, "dup2 failed");
  }

  ~stdin_data()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    if (dup2(m_saved.get(), STDIN_FILENO) == -1) {
      std::abort();
    }
    m_saved.reset();
  }

 private:
  fd_guard m_saved;
  fd_guard m_read;
  fd_guard m_write;
};

class terminal_stdin {
 public:
  terminal_stdin()
      : m_master(posix_openpt(O_RDWR | O_NOCTTY))
  {
    require_posix(m_master.get() != -1, "posix_openpt failed");
    require_posix(grantpt(m_master.get()) == 0, "grantpt failed");
    require_posix(unlockpt(m_master.get()) == 0, "unlockpt failed");
    const char* slave_name = ptsname(m_master.get());
    require_posix(slave_name != nullptr, "ptsname failed");
    m_slave.reset(open(slave_name, O_RDWR | O_NOCTTY));
    require_posix(m_slave.get() != -1, "open pty slave failed");
    winsize size = {
        .ws_row    = 24,
        .ws_col    = 80,
        .ws_xpixel = 640,
        .ws_ypixel = 480,
    };
    require_posix(ioctl(m_slave.get(), TIOCSWINSZ, &size) == 0, "ioctl TIOCSWINSZ failed");
    m_saved.reset(dup(STDIN_FILENO));
    require_posix(m_saved.get() != -1, "dup failed");
    require_posix(dup2(m_slave.get(), STDIN_FILENO) != -1, "dup2 failed");
  }

  ~terminal_stdin()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    if (dup2(m_saved.get(), STDIN_FILENO) == -1) {
      std::abort();
    }
    m_saved.reset();
  }

 private:
  fd_guard m_master;
  fd_guard m_slave;
  fd_guard m_saved;
};

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static std::string serialize(const protobuf::Message& message)
{
  std::string payload;
  payload.resize(message.ByteSizeLong());
  assert(message.SerializeToArray(payload.data(), static_cast<int>(payload.size())));
  return payload;
}

static protobuf::Message read_message(websocket::stream<tcp::socket>& ws)
{
  boost::beast::flat_buffer buffer;
  ws.read(buffer);
  auto payload = boost::beast::buffers_to_string(buffer.data());
  protobuf::Message message;
  const auto parsed = message.ParseFromArray(payload.data(), static_cast<int>(payload.size()));
  assert(parsed);
  return message;
}

static void write_message(websocket::stream<tcp::socket>& ws, const protobuf::Message& message)
{
  ws.binary(true);
  auto payload = serialize(message);
  ws.write(boost::asio::buffer(payload));
}

static rstream::webtty::byte_vector bytes_from_string(const std::string& value)
{
  return rstream::webtty::byte_vector(value.begin(), value.end());
}

static std::string string_from_bytes(const rstream::webtty::byte_vector& value)
{
  return std::string(value.begin(), value.end());
}

static rstream::webtty::byte_vector sha256_message(const google::protobuf::Message& message)
{
  std::string bytes;
  bytes.resize(message.ByteSizeLong());
  if (!bytes.empty()) {
    google::protobuf::io::ArrayOutputStream array_stream(bytes.data(), static_cast<int>(bytes.size()));
    google::protobuf::io::CodedOutputStream coded_stream(&array_stream);
    coded_stream.SetSerializationDeterministic(true);
    const auto serialized = message.SerializeToCodedStream(&coded_stream);
    assert(serialized);
    assert(!coded_stream.HadError());
  }
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

static void to_proto(protobuf::EndpointIdentity& dst, const rstream::webtty::endpoint_identity_public& src)
{
  dst.set_signing_key_id(string_from_bytes(src.m_signing_key_id));
  dst.set_signing_public_key(string_from_bytes(src.m_signing_public_key));
  dst.set_signature_suite(protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  dst.set_encryption_key_id(string_from_bytes(src.m_encryption_key_id));
  dst.set_encryption_public_key(string_from_bytes(src.m_encryption_public_key));
  dst.set_key_envelope_suite(protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
}

static void from_proto(rstream::webtty::key_envelope& dst, const protobuf::KeyEnvelope& src)
{
  dst.m_recipient_key_id = bytes_from_string(src.recipient_key_id());
  dst.m_encapsulated_key = bytes_from_string(src.encapsulated_key());
  dst.m_wrapped_key      = bytes_from_string(src.wrapped_key());
}

static void from_proto(rstream::webtty::session_key_grant& dst, const protobuf::SessionKeyGrant& src)
{
  dst.m_payload_suite  = static_cast<rstream::webtty::payload_cipher_suite>(src.payload_suite());
  dst.m_payload_key_id = bytes_from_string(src.payload_key_id());
  dst.m_key_envelopes.clear();
  for (const auto& envelope : src.key_envelopes()) {
    rstream::webtty::key_envelope converted;
    from_proto(converted, envelope);
    dst.m_key_envelopes.push_back(std::move(converted));
  }
  dst.m_key_context        = bytes_from_string(src.key_context());
  dst.m_key_envelope_suite = static_cast<rstream::webtty::key_envelope_suite>(src.key_envelope_suite());
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

static protobuf::Message server_hello_message(const rstream::webtty::endpoint_identity& identity, const std::string& session_id)
{
  std::error_code error_code;
  auto public_identity = rstream::webtty::public_endpoint_identity(identity);
  rstream::webtty::server_proof_transcript transcript;
  transcript.m_transport                = "websocket";
  transcript.m_session_id               = session_id;
  transcript.m_server_signing_key_id    = public_identity.m_signing_key_id;
  transcript.m_server_encryption_key_id = public_identity.m_encryption_key_id;
  transcript.m_server_nonce             = bytes_from_string("websocket-client-runtime-server-nonce");
  transcript.m_auth_requirement         = rstream::webtty::auth_requirement::client_proof;
  transcript.m_payload_suites           = {rstream::webtty::payload_cipher_suite::aes_256_gcm};
  transcript.m_key_envelope_suites      = {rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm};
  transcript.m_signature_suites         = {rstream::webtty::signature_suite::ecdsa_p256_sha256};
  rstream::webtty::byte_vector transcript_hash;
  rstream::webtty::byte_vector signature;
  rstream::webtty::sign_webtty_server_proof_transcript(transcript_hash, signature, identity.m_signing, transcript, error_code);
  assert(!error_code);
  protobuf::Message message;
  auto* hello = message.mutable_server_hello();
  hello->set_protocol_version(protobuf::PROTOCOL_VERSION_WEBTTY_1);
  hello->set_session_nonce(string_from_bytes(transcript.m_server_nonce));
  to_proto(*hello->mutable_server_identity(), public_identity);
  hello->add_payload_suites(protobuf::PAYLOAD_CIPHER_SUITE_AES_256_GCM);
  hello->add_key_envelope_suites(protobuf::KEY_ENVELOPE_SUITE_HPKE_X25519_HKDF_SHA256_AES_256_GCM);
  hello->add_signature_suites(protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  hello->set_auth_requirement(protobuf::AUTH_REQUIREMENT_CLIENT_PROOF);
  hello->set_session_id(session_id);
  auto* proof = hello->mutable_server_proof();
  proof->set_signature_suite(protobuf::SIGNATURE_SUITE_ECDSA_P256_SHA256);
  proof->set_signing_key_id(string_from_bytes(public_identity.m_signing_key_id));
  proof->set_transcript_hash(string_from_bytes(transcript_hash));
  proof->set_signature(string_from_bytes(signature));
  return message;
}

static void verify_client_proof(const protobuf::Open& open,
                                const protobuf::ServerHello& hello,
                                const rstream::webtty::endpoint_identity& authorized_client,
                                const std::string& expected_credential = "")
{
  assert(open.has_client_proof());
  const auto& proof = open.client_proof();
  assert(bytes_from_string(proof.signing_key_id()) == authorized_client.m_signing.m_key_id);
  assert(bytes_from_string(proof.signing_public_key()) == authorized_client.m_signing.m_public_key);
  if (expected_credential.empty()) {
    assert(!proof.has_credential());
  }
  else {
    assert(proof.has_credential());
    assert(proof.credential().value() == expected_credential);
  }
  rstream::webtty::client_proof_transcript transcript;
  transcript.m_transport                = "websocket";
  transcript.m_session_id               = hello.session_id();
  transcript.m_server_signing_key_id    = bytes_from_string(hello.server_identity().signing_key_id());
  transcript.m_server_encryption_key_id = bytes_from_string(hello.server_identity().encryption_key_id());
  transcript.m_server_nonce             = bytes_from_string(hello.session_nonce());
  transcript.m_auth_requirement         = rstream::webtty::auth_requirement::client_proof;
  transcript.m_payload_suite            = rstream::webtty::payload_cipher_suite::aes_256_gcm;
  transcript.m_key_envelope_suite       = rstream::webtty::key_envelope_suite::hpke_x25519_hkdf_sha256_aes_256_gcm;
  transcript.m_session_key_grant_hash   = open.has_session_key_grant() ? sha256_message(open.session_key_grant()) : sha256_message(protobuf::SessionKeyGrant());
  transcript.m_command_config_hash      = open.has_config() ? sha256_message(open.config()) : sha256_message(protobuf::Config());
  transcript.m_client_signing_key_id    = authorized_client.m_signing.m_key_id;
  transcript.m_client_credential_hash   = proof.has_credential() ? sha256_bytes(proof.credential().value()) : sha256_bytes("");
  transcript.m_issued_at                = proof.issued_at();
  transcript.m_expires_at               = proof.expires_at();
  rstream::webtty::byte_vector transcript_hash;
  std::error_code error_code;
  rstream::webtty::hash_webtty_client_proof_transcript(transcript_hash, transcript, error_code);
  assert(!error_code);
  assert(transcript_hash == bytes_from_string(proof.transcript_hash()));
  rstream::webtty::verify_webtty_client_proof_transcript(authorized_client.m_signing.m_public_key, transcript, bytes_from_string(proof.signature()), error_code);
  assert(!error_code);
}

class fake_websocket_server {
 public:
  fake_websocket_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_websocket_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        assert(request.target() == "/session");

        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(open.open().config().options().interactive());
        assert(!open.open().config().options().allocate_tty());
        assert(open.open().config().options().send_heartbeat());

        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        protobuf::Message inbound_heartbeat;
        inbound_heartbeat.mutable_heartbeat();
        write_message(ws, inbound_heartbeat);

        bool saw_stdin     = false;
        bool saw_stdin_eos = false;
        bool saw_heartbeat = false;
        while (!saw_stdin || !saw_stdin_eos || !saw_heartbeat) {
          auto message = read_message(ws);
          if (message.payload_case() == protobuf::Message::PayloadCase::kData) {
            assert(message.data().type() == protobuf::Data::TYPE_STDIN);
            if (message.data().has_eos()) {
              saw_stdin_eos = true;
            }
            else if (message.data().data() == "client-input") {
              saw_stdin = true;
            }
          }
          else if (message.payload_case() == protobuf::Message::PayloadCase::kHeartbeat) {
            saw_heartbeat = true;
          }
          else {
            std::cerr << "unexpected websocket client message: " << message.payload_case() << std::endl;
            assert(false);
          }
        }

        protobuf::Message close;
        close.mutable_close()->set_return_code(21);
        write_message(ws, close);
        boost::system::error_code error_code;
        ws.close(websocket::close_code::normal, error_code);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_early_exit_websocket_server {
 public:
  fake_early_exit_websocket_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_early_exit_websocket_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(open.open().config().options().interactive());
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        auto input = read_message(ws);
        assert(input.payload_case() == protobuf::Message::PayloadCase::kData);
        assert(input.data().type() == protobuf::Data::TYPE_STDIN);
        assert(input.data().data() == "accepted-input");
        assert(!input.data().has_eos());

        protobuf::Message close;
        close.mutable_close()->set_return_code(0);
        write_message(ws, close);
        boost::system::error_code error_code;
        ws.close(websocket::close_code::normal, error_code);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_e2e_websocket_server {
 public:
  explicit fake_e2e_websocket_server(const rstream::webtty::endpoint_identity& authorized_client, const std::string& expected_client_credential = "")
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port())),
        m_authorized_client(authorized_client),
        m_expected_client_credential(expected_client_credential)
  {
    std::error_code error_code;
    rstream::webtty::generate_endpoint_identity(m_identity, error_code);
    assert(!error_code);
  }

  ~fake_e2e_websocket_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  const rstream::webtty::endpoint_identity& identity() const
  {
    return m_identity;
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        std::error_code error_code;
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        assert(request.target() == "/e2e");

        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto hello = server_hello_message(m_identity, "websocket-client-runtime-session");
        write_message(ws, hello);
        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(open.open().has_session_key_grant());
        verify_client_proof(open.open(), hello.server_hello(), m_authorized_client, m_expected_client_credential);

        rstream::webtty::session_key_grant session_key_grant;
        from_proto(session_key_grant, open.open().session_key_grant());
        auto server_crypto = rstream::webtty::make_e2e_server_payload_crypto(session_key_grant, m_identity.m_encryption, error_code);
        assert(!error_code);
        assert(server_crypto);

        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        bool saw_stdin     = false;
        bool saw_stdin_eos = false;
        while (!saw_stdin || !saw_stdin_eos) {
          auto message = read_message(ws);
          assert(message.payload_case() == protobuf::Message::PayloadCase::kData);
          assert(message.data().type() == protobuf::Data::TYPE_STDIN);
          if (message.data().has_eos()) {
            saw_stdin_eos = true;
          }
          else {
            assert(message.data().has_encrypted_data());
            rstream::webtty::encrypted_payload encrypted;
            rstream::webtty::byte_vector plaintext;
            from_proto(encrypted, message.data().encrypted_data());
            server_crypto->decrypt(rstream::webtty::payload_stream::std_in, encrypted, plaintext, error_code);
            assert(!error_code);
            assert(string_from_bytes(plaintext) == "websocket-e2e-input");
            saw_stdin = true;
          }
        }

        protobuf::Message close;
        close.mutable_close()->set_return_code(23);
        write_message(ws, close);
        boost::system::error_code close_error;
        ws.close(websocket::close_code::normal, close_error);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  rstream::webtty::endpoint_identity m_identity;
  rstream::webtty::endpoint_identity m_authorized_client;
  std::string m_expected_client_credential;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

class fake_terminal_websocket_server {
 public:
  fake_terminal_websocket_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_terminal_websocket_server()
  {
    join();
  }

  unsigned short port() const
  {
    return m_acceptor.local_endpoint().port();
  }

  void start()
  {
    m_thread = std::thread([this] {
      try {
        tcp::socket socket(m_io_context);
        m_acceptor.accept(socket);
        boost::beast::flat_buffer http_buffer;
        http::request<http::string_body> request;
        http::read(socket, http_buffer, request);
        assert(request.target() == "/tty");

        websocket::stream<tcp::socket> ws(std::move(socket));
        ws.accept(request);

        auto open = read_message(ws);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(!open.open().config().options().interactive());
        assert(open.open().config().options().allocate_tty());
        assert(!open.open().config().options().send_heartbeat());

        protobuf::Message ack;
        ack.mutable_ack();
        write_message(ws, ack);

        auto parameter = read_message(ws);
        assert(parameter.payload_case() == protobuf::Message::PayloadCase::kParameter);
        assert(parameter.parameter().has_terminal_size());
        assert(parameter.parameter().terminal_size().row() == 24);
        assert(parameter.parameter().terminal_size().col() == 80);

        protobuf::Message close;
        close.mutable_close()->set_return_code(22);
        write_message(ws, close);
        boost::system::error_code error_code;
        ws.close(websocket::close_code::normal, error_code);
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  void join()
  {
    if (m_thread.joinable()) {
      m_thread.join();
    }
    if (m_exception) {
      std::rethrow_exception(m_exception);
    }
  }

 private:
  boost::asio::io_context m_io_context;
  tcp::acceptor m_acceptor;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

static void check_websocket_client_sends_open_stdin_eos_and_heartbeat()
{
  fake_websocket_server server;
  server.start();

  stdin_data input("client-input");
  boost::asio::io_context io_context;
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/session"),
      .m_protocol_config  = {
          .m_protocol_type = rstream::webtty::protocol::type::websocket,
          .m_options       = {
              .m_interactive    = true,
              .m_allocate_tty   = false,
              .m_send_heartbeat = true,
          },
          .m_env_vars = {},
          .m_cmd_args = {"/bin/sh", "-c", "unused"},
          .m_workdir  = {},
          .m_username = {},
      },
  };
  rstream::webtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = rstream::test::timeout_ms(5000),
              .m_close     = rstream::test::timeout_ms(5000),
              .m_heartbeat = 10,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  bool timed_out  = false;
  {
    rstream::webtty::client client(io_context.get_executor(), config, settings);
    input.restore();
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(rstream::test::timeout(std::chrono::seconds(10)));
    deadline.async_wait([&](const std::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const std::error_code& error_code, int code) {
      result      = error_code;
      return_code = code;
      done        = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
  }
  server.join();
  assert(!result);
  assert(return_code == 21);
}

static void check_websocket_client_e2e_sends_encrypted_stdin()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  const auto client_credential = std::string("{\"type\":\"test.workspace.credential\",\"v\":1}");
  fake_e2e_websocket_server server(client_identity, client_credential);
  server.start();

  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x34);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0004");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"websocket\"}");
  crypto_config.m_recipients     = {{server.identity().m_encryption.m_key_id, server.identity().m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);

  stdin_data input("websocket-e2e-input");
  boost::asio::io_context io_context;
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/e2e"),
      .m_protocol_config  = {
          .m_protocol_type = rstream::webtty::protocol::type::websocket,
          .m_options       = {
              .m_interactive    = true,
              .m_allocate_tty   = false,
              .m_send_heartbeat = false,
          },
          .m_env_vars = {},
          .m_cmd_args = {"/bin/sh", "-c", "unused"},
          .m_workdir  = {},
          .m_username = {},
      },
  };
  rstream::webtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = rstream::test::timeout_ms(5000),
              .m_close     = rstream::test::timeout_ms(5000),
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size       = 64 * 1024,
      .m_payload_crypto           = client_crypto,
      .m_endpoint_identity        = client_identity,
      .m_expected_server_identity = rstream::webtty::public_endpoint_identity(server.identity()),
      .m_client_credential        = bytes_from_string(client_credential),
  };

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  bool timed_out  = false;
  {
    rstream::webtty::client client(io_context.get_executor(), config, settings);
    input.restore();
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(rstream::test::timeout(std::chrono::seconds(10)));
    deadline.async_wait([&](const std::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const std::error_code& error_code, int code) {
      result      = error_code;
      return_code = code;
      done        = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
  }
  server.join();
  assert(!result);
  assert(return_code == 23);
}

static void check_websocket_client_accepts_remote_exit_during_stdin_shutdown(bool keep_stdin_open)
{
  fake_early_exit_websocket_server server;
  server.start();

  stdin_data input("accepted-input", keep_stdin_open);
  boost::asio::io_context io_context;
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/session"),
      .m_protocol_config  = {
          .m_protocol_type = rstream::webtty::protocol::type::websocket,
          .m_options       = {
              .m_interactive    = true,
              .m_allocate_tty   = false,
              .m_send_heartbeat = false,
          },
          .m_env_vars = {},
          .m_cmd_args = {"/bin/sh", "-c", "unused"},
          .m_workdir  = {},
          .m_username = {},
      },
  };
  rstream::webtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = rstream::test::timeout_ms(5000),
              .m_close     = rstream::test::timeout_ms(5000),
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  input.restore();
  bool timed_out = false;
  boost::asio::steady_timer deadline(io_context);
  deadline.expires_after(rstream::test::timeout(std::chrono::seconds(10)));
  deadline.async_wait([&](const std::error_code& error_code) {
    if (!error_code && !done) {
      timed_out = true;
      client.cancel();
    }
  });
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
    done        = true;
    deadline.cancel();
  });
  io_context.run();
  server.join();
  assert(done);
  assert(!timed_out);
  assert(!result);
  assert(return_code == 0);
}

static void check_websocket_client_sends_terminal_size_when_tty_allocated()
{
  fake_terminal_websocket_server server;
  server.start();

  terminal_stdin terminal;
  boost::asio::io_context io_context;
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = std::string("/tty"),
      .m_protocol_config  = {
          .m_protocol_type = rstream::webtty::protocol::type::websocket,
          .m_options       = {
              .m_interactive    = false,
              .m_allocate_tty   = true,
              .m_send_heartbeat = false,
          },
          .m_env_vars = {},
          .m_cmd_args = {"/bin/sh", "-c", "unused"},
          .m_workdir  = {},
          .m_username = {},
      },
  };
  rstream::webtty::settings_client settings = {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = rstream::test::timeout_ms(5000),
              .m_close     = rstream::test::timeout_ms(5000),
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };

  std::error_code result;
  int return_code = -1;
  bool done       = false;
  bool timed_out  = false;
  {
    rstream::webtty::client client(io_context.get_executor(), config, settings);
    boost::asio::steady_timer deadline(io_context);
    deadline.expires_after(rstream::test::timeout(std::chrono::seconds(10)));
    deadline.async_wait([&](const std::error_code& error_code) {
      if (!error_code && !done) {
        timed_out = true;
        client.cancel();
      }
    });
    client.async_run([&](const std::error_code& error_code, int code) {
      result      = error_code;
      return_code = code;
      done        = true;
      deadline.cancel();
    });
    io_context.run();
    assert(done);
    assert(!timed_out);
  }
  terminal.restore();
  server.join();
  assert(!result);
  assert(return_code == 22);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_websocket_client_sends_open_stdin_eos_and_heartbeat();
  check_websocket_client_e2e_sends_encrypted_stdin();
  check_websocket_client_accepts_remote_exit_during_stdin_shutdown(true);
  check_websocket_client_accepts_remote_exit_during_stdin_shutdown(false);
  check_websocket_client_sends_terminal_size_when_tty_allocated();
  return 0;
}
