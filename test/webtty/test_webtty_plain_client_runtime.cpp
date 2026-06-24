// See LICENSE file in the project root for license information.

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <arpa/inet.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <unistd.h>

#include <rstream/webtty/client.hpp>
#include <rstream/webtty/error.hpp>
#include <rstream/webtty/protobuf/messages.pb.h>
#include <rstream/webtty/webtty.hpp>

namespace protobuf = rstream::webtty::protobuf;
using tcp          = boost::asio::ip::tcp;

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

  int release()
  {
    auto fd = m_fd;
    m_fd    = -1;
    return fd;
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

class fd_capture {
 public:
  explicit fd_capture(int target)
      : m_target(target)
  {
    int fds[2] = {-1, -1};
    require_posix(pipe(fds) == 0, "pipe failed");
    m_read.reset(fds[0]);
    fd_guard write(fds[1]);
    m_saved.reset(dup(target));
    require_posix(m_saved.get() != -1, "dup failed");
    require_posix(dup2(write.get(), target) != -1, "dup2 failed");
  }

  ~fd_capture()
  {
    restore();
  }

  void restore()
  {
    if (m_saved.get() == -1) {
      return;
    }
    if (dup2(m_saved.get(), m_target) == -1) {
      std::abort();
    }
    m_saved.reset();
  }

  std::string read_all()
  {
    restore();
    std::string out;
    std::array<char, 1024> buffer{};
    while (true) {
      auto n = read(m_read.get(), buffer.data(), buffer.size());
      if (n == 0) {
        break;
      }
      assert(n > 0);
      out.append(buffer.data(), static_cast<std::size_t>(n));
    }
    return out;
  }

 private:
  int m_target;
  fd_guard m_saved;
  fd_guard m_read;
};

class stdin_data {
 public:
  explicit stdin_data(const std::string& data)
  {
    int fds[2] = {-1, -1};
    require_posix(pipe(fds) == 0, "pipe failed");
    m_read.reset(fds[0]);
    fd_guard write_fd(fds[1]);
    std::size_t offset = 0;
    while (offset < data.size()) {
      auto n = write(write_fd.get(), data.data() + offset, data.size() - offset);
      require_posix(n > 0, "write failed");
      offset += static_cast<std::size_t>(n);
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
};

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
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

static void write_message(tcp::socket& socket, const protobuf::Message& message)
{
  const auto size          = static_cast<std::uint32_t>(message.ByteSizeLong());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  std::vector<char> payload(size);
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));
  if (!payload.empty()) {
    boost::asio::write(socket, boost::asio::buffer(payload));
  }
}

static void write_payload(tcp::socket& socket, const std::string& payload)
{
  auto size                = static_cast<std::uint32_t>(payload.size());
  std::uint32_t frame_size = htonl(size);
  boost::asio::write(socket, boost::asio::buffer(&frame_size, sizeof(frame_size)));
  boost::asio::write(socket, boost::asio::buffer(payload));
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

static void to_proto(protobuf::KeyEnvelope& dst, const rstream::webtty::key_envelope& src)
{
  dst.set_recipient_key_id(string_from_bytes(src.m_recipient_key_id));
  dst.set_encapsulated_key(string_from_bytes(src.m_encapsulated_key));
  dst.set_wrapped_key(string_from_bytes(src.m_wrapped_key));
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

static protobuf::Message data_message(protobuf::Data::Type type, const std::string& data)
{
  protobuf::Message message;
  message.mutable_data()->set_type(type);
  message.mutable_data()->set_data(data);
  return message;
}

static protobuf::Message encrypted_data_message(protobuf::Data::Type type, const rstream::webtty::encrypted_payload& encrypted)
{
  protobuf::Message message;
  auto* data = message.mutable_data();
  data->set_type(type);
  to_proto(*data->mutable_encrypted_data(), encrypted);
  return message;
}

static protobuf::Message server_hello_message(const rstream::webtty::endpoint_identity& identity, const std::string& session_id)
{
  std::error_code error_code;
  auto public_identity = rstream::webtty::public_endpoint_identity(identity);
  rstream::webtty::server_proof_transcript transcript;
  transcript.m_transport                = "plain";
  transcript.m_session_id               = session_id;
  transcript.m_server_signing_key_id    = public_identity.m_signing_key_id;
  transcript.m_server_encryption_key_id = public_identity.m_encryption_key_id;
  transcript.m_server_nonce             = bytes_from_string("plain-client-runtime-server-nonce");
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
  transcript.m_transport                = "plain";
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

class fake_plain_server {
 public:
  fake_plain_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_plain_server()
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
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        assert(!open.open().config().options().interactive());
        assert(!open.open().config().options().allocate_tty());
        assert(!open.open().config().options().send_heartbeat());
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        write_message(socket, data_message(protobuf::Data::TYPE_STDOUT, "client-stdout"));
        write_message(socket, data_message(protobuf::Data::TYPE_STDERR, "client-stderr"));
        protobuf::Message close;
        close.mutable_close()->set_return_code(13);
        write_message(socket, close);
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

class fake_e2e_plain_server {
 public:
  explicit fake_e2e_plain_server(const rstream::webtty::endpoint_identity& authorized_client, const std::string& expected_client_credential = "")
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port())),
        m_authorized_client(authorized_client),
        m_expected_client_credential(expected_client_credential)
  {
    std::error_code error_code;
    rstream::webtty::generate_endpoint_identity(m_identity, error_code);
    assert(!error_code);
  }

  ~fake_e2e_plain_server()
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
        auto hello = server_hello_message(m_identity, "plain-client-runtime-session");
        write_message(socket, hello);
        auto open = read_message(socket);
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
        write_message(socket, ack);

        bool saw_stdin     = false;
        bool saw_stdin_eos = false;
        while (!saw_stdin || !saw_stdin_eos) {
          auto message = read_message(socket);
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
            assert(string_from_bytes(plaintext) == "e2e-client-input");
            saw_stdin = true;
          }
        }

        rstream::webtty::encrypted_payload encrypted_stdout;
        server_crypto->encrypt(rstream::webtty::payload_stream::std_out, bytes_from_string("e2e-client-stdout"), encrypted_stdout, error_code);
        assert(!error_code);
        write_message(socket, encrypted_data_message(protobuf::Data::TYPE_STDOUT, encrypted_stdout));

        rstream::webtty::encrypted_payload encrypted_stderr;
        server_crypto->encrypt(rstream::webtty::payload_stream::std_err, bytes_from_string("e2e-client-stderr"), encrypted_stderr, error_code);
        assert(!error_code);
        write_message(socket, encrypted_data_message(protobuf::Data::TYPE_STDERR, encrypted_stderr));

        protobuf::Message close;
        close.mutable_close()->set_return_code(17);
        write_message(socket, close);
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

class fake_error_server {
 public:
  fake_error_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_error_server()
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
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message error;
        error.mutable_error()->set_msg("server refused");
        write_message(socket, error);
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

class fake_invalid_payload_server {
 public:
  fake_invalid_payload_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port()))
  {
  }

  ~fake_invalid_payload_server()
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
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        write_payload(socket, "not-a-protobuf-message");
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

class fake_cancel_server {
 public:
  fake_cancel_server()
      : m_acceptor(m_io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), unused_tcp_port())),
        m_ack_sent(m_ack_promise.get_future())
  {
  }

  ~fake_cancel_server()
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
        auto open = read_message(socket);
        assert(open.payload_case() == protobuf::Message::PayloadCase::kOpen);
        protobuf::Message ack;
        ack.mutable_ack();
        write_message(socket, ack);
        m_ack_promise.set_value();
        auto error = read_message(socket);
        assert(error.payload_case() == protobuf::Message::PayloadCase::kError);
        assert(!error.error().msg().empty());
      }
      catch (...) {
        m_exception = std::current_exception();
      }
    });
  }

  bool wait_for_ack()
  {
    return m_ack_sent.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
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
  std::promise<void> m_ack_promise;
  std::future<void> m_ack_sent;
  std::thread m_thread;
  std::exception_ptr m_exception;
};

static rstream::webtty::client::config plain_client_config(unsigned short port)
{
  return {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(port)),
      .m_websocket_target = boost::none,
      .m_protocol_config  = {
           .m_protocol_type = rstream::webtty::protocol::type::plain,
           .m_options       = {
                     .m_interactive    = false,
                     .m_allocate_tty   = false,
                     .m_send_heartbeat = false,
          },
           .m_env_vars = {},
           .m_cmd_args = {"/bin/sh", "-c", "unused"},
           .m_workdir  = {},
           .m_username = {},
      },
  };
}

static rstream::webtty::settings_client plain_client_settings()
{
  return {
      .m_common = {
          .m_mtu         = 1024 * 1024,
          .m_timeouts_ms = {
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };
}

static void check_plain_client_processes_server_messages()
{
  fake_plain_server server;
  server.start();

  fd_capture stdout_capture(STDOUT_FILENO);
  fd_capture stderr_capture(STDERR_FILENO);

  boost::asio::io_context io_context;
  rstream::webtty::client::config config = {
      .m_address          = rstream::io::address(std::string("127.0.0.1:") + std::to_string(server.port())),
      .m_websocket_target = boost::none,
      .m_protocol_config  = {
           .m_protocol_type = rstream::webtty::protocol::type::plain,
           .m_options       = {
                     .m_interactive    = false,
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
              .m_open      = 5000,
              .m_close     = 5000,
              .m_heartbeat = 0,
          },
      },
      .m_std_in_buffer_size = 64 * 1024,
  };
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = -1;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(!result);
  assert(return_code == 13);
  assert(stdout_capture.read_all().find("client-stdout") != std::string::npos);
  assert(stderr_capture.read_all().find("client-stderr") != std::string::npos);
}

static void check_plain_client_e2e_sends_stdin_and_processes_server_messages()
{
  std::error_code error_code;
  rstream::webtty::endpoint_identity client_identity;
  rstream::webtty::generate_endpoint_identity(client_identity, error_code);
  assert(!error_code);
  const auto client_credential = std::string("{\"type\":\"test.workspace.credential\",\"v\":1}");
  fake_e2e_plain_server server(client_identity, client_credential);
  server.start();

  rstream::webtty::e2e_payload_crypto_config crypto_config;
  crypto_config.m_payload_key    = rstream::webtty::byte_vector(32, 0x32);
  crypto_config.m_payload_key_id = bytes_from_string("payload-key-0002");
  crypto_config.m_key_context    = bytes_from_string("{\"transport\":\"plain\"}");
  crypto_config.m_recipients     = {{server.identity().m_encryption.m_key_id, server.identity().m_encryption.m_public_key}};
  auto client_crypto             = rstream::webtty::make_e2e_client_payload_crypto(crypto_config, error_code);
  assert(!error_code);
  stdin_data input("e2e-client-input");
  fd_capture stdout_capture(STDOUT_FILENO);
  fd_capture stderr_capture(STDERR_FILENO);

  boost::asio::io_context io_context;
  auto config                                      = plain_client_config(server.port());
  config.m_protocol_config.m_options.m_interactive = true;
  auto settings                                    = plain_client_settings();
  settings.m_payload_crypto                        = client_crypto;
  settings.m_endpoint_identity                     = client_identity;
  settings.m_expected_server_identity              = rstream::webtty::public_endpoint_identity(server.identity());
  settings.m_client_credential                     = bytes_from_string(client_credential);
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  input.restore();

  std::error_code result;
  int return_code = -1;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(!result);
  assert(return_code == 17);
  assert(stdout_capture.read_all().find("e2e-client-stdout") != std::string::npos);
  assert(stderr_capture.read_all().find("e2e-client-stderr") != std::string::npos);
}

static void check_plain_client_reports_server_error_during_open()
{
  fake_error_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(result == rstream::webtty::error::make_error_code(rstream::webtty::error::code::server_error));
  assert(return_code == -1);
}

static void check_plain_client_rejects_invalid_payload_after_open()
{
  fake_invalid_payload_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
  });
  io_context.run();
  server.join();

  assert(result == rstream::webtty::error::make_error_code(rstream::webtty::error::code::protocol_error));
  assert(return_code == -1);
}

static void check_plain_client_cancel_after_open_sends_error()
{
  fake_cancel_server server;
  server.start();

  boost::asio::io_context io_context;
  auto config   = plain_client_config(server.port());
  auto settings = plain_client_settings();
  rstream::webtty::client client(io_context.get_executor(), config, settings);
  std::error_code result;
  int return_code = 0;
  bool done       = false;
  client.async_run([&](const std::error_code& error_code, int code) {
    result      = error_code;
    return_code = code;
    done        = true;
  });
  std::thread io_thread([&] {
    io_context.run();
  });
  assert(server.wait_for_ack());
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  client.cancel();
  io_thread.join();
  server.join();

  assert(done);
  assert(result == rstream::webtty::error::make_error_code(rstream::webtty::error::code::operation_aborted));
  assert(return_code == -1);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_plain_client_processes_server_messages();
  check_plain_client_e2e_sends_stdin_and_processes_server_messages();
  check_plain_client_reports_server_error_during_open();
  check_plain_client_rejects_invalid_payload_after_open();
  check_plain_client_cancel_after_open_sends_error();
  return 0;
}
