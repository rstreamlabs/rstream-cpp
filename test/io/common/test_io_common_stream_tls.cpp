// See LICENSE file in the project root for license information.

#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/buffer.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/write.hpp>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include <rstream/core/completion_handler.hpp>
#include <rstream/io/detail/stream/error.hpp>
#include <rstream/io/detail/stream/stream_socket_ssl.hpp>
#include <rstream/io/stream.hpp>

using tcp = boost::asio::ip::tcp;

struct test_certificate {
  std::string cert;
  std::string key;
};

template <class T, class Free>
using openssl_ptr = std::unique_ptr<T, Free>;

static std::string bio_string(BIO& bio)
{
  BUF_MEM* buffer = nullptr;
  BIO_get_mem_ptr(&bio, &buffer);
  if (!buffer || !buffer->data) {
    return {};
  }
  return {buffer->data, buffer->length};
}

static test_certificate generate_test_certificate()
{
  openssl_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> key_context(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
  if (!key_context || EVP_PKEY_keygen_init(key_context.get()) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(key_context.get(), 2048) <= 0) {
    throw std::runtime_error("failed to initialize test certificate key generation");
  }

  EVP_PKEY* raw_key = nullptr;
  if (EVP_PKEY_keygen(key_context.get(), &raw_key) <= 0) {
    throw std::runtime_error("failed to generate test certificate key");
  }
  openssl_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(raw_key, EVP_PKEY_free);
  openssl_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
  if (!cert) {
    throw std::runtime_error("failed to allocate test certificate");
  }

  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
  X509_gmtime_adj(X509_get_notBefore(cert.get()), -60);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), 24 * 60 * 60);
  X509_set_pubkey(cert.get(), key.get());

  openssl_ptr<X509_NAME, decltype(&X509_NAME_free)> name(X509_NAME_new(), X509_NAME_free);
  if (!name || X509_NAME_add_entry_by_txt(name.get(), "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) <= 0
      || X509_set_subject_name(cert.get(), name.get()) <= 0 || X509_set_issuer_name(cert.get(), name.get()) <= 0
      || X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0) {
    throw std::runtime_error("failed to sign test certificate");
  }

  openssl_ptr<BIO, decltype(&BIO_free)> cert_bio(BIO_new(BIO_s_mem()), BIO_free);
  openssl_ptr<BIO, decltype(&BIO_free)> key_bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!cert_bio || !key_bio || PEM_write_bio_X509(cert_bio.get(), cert.get()) <= 0
      || PEM_write_bio_PrivateKey(key_bio.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) <= 0) {
    throw std::runtime_error("failed to encode test certificate");
  }
  return {bio_string(*cert_bio), bio_string(*key_bio)};
}

static const test_certificate& test_certificate_pem()
{
  static const test_certificate certificate = generate_test_certificate();
  return certificate;
}

static void assert_stream_error(const boost::system::error_code& actual, rstream::io::detail::stream::error::code expected)
{
  assert(actual.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(actual.value() == static_cast<int>(expected));
}

static std::string encode_query_value(const std::string& value)
{
  std::ostringstream out;
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      out << static_cast<char>(c);
    }
    else {
      out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
  }
  return out.str();
}

static unsigned short unused_tcp_port()
{
  boost::asio::io_context io_context;
  tcp::acceptor acceptor(io_context, tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 0));
  return acceptor.local_endpoint().port();
}

static rstream::io::stream::endpoint resolve_one(boost::asio::io_context& io_context, const std::string& uri)
{
  rstream::io::stream::resolver resolver(io_context.get_executor());
  rstream::io::stream::resolver::results_type results;
  boost::system::error_code error_code;
  bool completed = false;
  resolver.async_resolve(uri, [&](const boost::system::error_code& error, const rstream::io::stream::resolver::results_type& resolved) {
    error_code = error;
    results    = resolved;
    completed  = true;
  });
  io_context.run();
  io_context.restart();
  assert(completed);
  assert(!error_code);
  assert(!results.empty());
  return results.front().endpoint();
}

class fake_stream_socket : public rstream::io::detail::stream::stream_socket_interface {
 public:
  explicit fake_stream_socket(const executor_type& executor)
      : rstream::io::detail::stream::stream_socket_interface(executor)
  {
  }

  void open(const endpoint_type& endpoint, boost::system::error_code& error_code) override
  {
    m_endpoint = endpoint;
    error_code.clear();
  }

  void close(boost::system::error_code& error_code) override
  {
    error_code.clear();
  }

  endpoint_type remote_endpoint(boost::system::error_code& error_code) override
  {
    error_code.clear();
    return m_endpoint;
  }

  bool is_secure() const override
  {
    return false;
  }

 private:
  void async_connect_internal(const endpoint_type& endpoint, async_connect_completion_handler&& handler) override
  {
    m_endpoint = endpoint;
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code());
  }

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler) override
  {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code(), boost::asio::buffer_size(buffer));
  }

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler) override
  {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code(), boost::asio::buffer_size(buffer));
  }

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler) override
  {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::asio::error::would_block, boost::asio::buffer_size(buffer));
  }

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler) override
  {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::asio::error::would_block, boost::asio::buffer_size(buffer));
  }

  void async_shutdown_send_internal(async_shutdown_send_completion_handler&& handler) override
  {
    rstream::core::invoke_completion_handler(get_executor(), std::move(handler), boost::system::error_code());
  }

  endpoint_type m_endpoint;
};

static rstream::io::detail::stream::ssl::config base_ssl_config()
{
  rstream::io::detail::stream::ssl::config config = {};
  config.m_tlsv12                                 = false;
  config.m_tlsv13                                 = false;
  config.m_peer_verification                      = false;
  config.m_request_peer_cert                      = false;
  config.m_client_rpk                             = false;
  config.m_server_rpk                             = false;
  return config;
}

static boost::system::error_code make_direct_ssl_socket(rstream::io::detail::stream::ssl::config config,
                                                        rstream::io::detail::stream::stream_socket_ssl::type type)
{
  boost::asio::io_context io_context;
  auto next_layer = std::make_shared<fake_stream_socket>(io_context.get_executor());
  try {
    rstream::io::detail::stream::stream_socket_ssl socket(next_layer, config, type);
    boost::system::error_code error_code;
    socket.open(rstream::io::stream::endpoint(), error_code);
    assert(!error_code);
    auto remote = socket.remote_endpoint(error_code);
    (void)remote;
    assert(!error_code);
    assert(socket.next_layer() == next_layer);
    assert(socket.is_secure());
    return {};
  }
  catch (const boost::system::system_error& error) {
    return error.code();
  }
}

static void assert_direct_ssl_config_fails(const rstream::io::detail::stream::ssl::config& config)
{
  const auto error_code = make_direct_ssl_socket(config, rstream::io::detail::stream::stream_socket_ssl::type::client);
  if (!error_code) {
    std::cerr << "SSL config unexpectedly succeeded"
              << " cert_type=" << config.m_cert_type.get_value_or("-")
              << " key_type=" << config.m_key_type.get_value_or("-")
              << " passphrase=" << static_cast<bool>(config.m_passphrase)
              << " cert=" << static_cast<bool>(config.m_cert)
              << " cert_file=" << static_cast<bool>(config.m_cert_file)
              << " key=" << static_cast<bool>(config.m_key)
              << " key_file=" << static_cast<bool>(config.m_key_file)
              << " tmp_dh=" << static_cast<bool>(config.m_tmp_dh_params)
              << " tmp_dh_file=" << static_cast<bool>(config.m_tmp_dh_params_file)
              << " cacert=" << static_cast<bool>(config.m_cacert)
              << " cacert_file=" << static_cast<bool>(config.m_cacert_file)
              << " capath=" << static_cast<bool>(config.m_capath)
              << " ciphers=" << config.m_ciphers.get_value_or("-")
              << " ciphers_tlsv13=" << config.m_ciphers_tlsv13.get_value_or("-")
              << std::endl;
  }
  assert(error_code);
}

static void assert_direct_ssl_config_succeeds(const rstream::io::detail::stream::ssl::config& config,
                                              rstream::io::detail::stream::stream_socket_ssl::type type)
{
  const auto error_code = make_direct_ssl_socket(config, type);
  if (error_code) {
    std::cerr << "unexpected SSL config error: " << error_code.category().name()
              << ":" << error_code.value() << " " << error_code.message() << std::endl;
  }
  assert(!error_code);
}

static void check_direct_tls_shutdown_path()
{
  boost::asio::io_context io_context;
  auto next_layer                    = std::make_shared<fake_stream_socket>(io_context.get_executor());
  auto config                        = base_ssl_config();
  config.m_async_shutdown_timeout_ms = 0;
  rstream::io::detail::stream::stream_socket_ssl socket(next_layer, config, rstream::io::detail::stream::stream_socket_ssl::type::client);

  bool shutdown_called = false;
  socket.async_shutdown([&](const boost::system::error_code& error_code) {
    (void)error_code;
    shutdown_called = true;
  });
  io_context.run_for(std::chrono::milliseconds(100));
  io_context.restart();
  assert(shutdown_called);

  boost::system::error_code close_error;
  socket.close(close_error);
  assert(!close_error);
  io_context.run_for(std::chrono::milliseconds(100));
}

template <class Predicate>
static void run_until(boost::asio::io_context& io_context, Predicate&& predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    io_context.run_one_for(std::chrono::milliseconds(50));
  }
  io_context.restart();
  assert(predicate());
}

class certificate_files {
 public:
  certificate_files()
  {
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    m_dir             = std::filesystem::temp_directory_path() / ("rstream-cpp-tls-" + suffix);
    std::filesystem::create_directories(m_dir);
    m_cert_file             = m_dir / "cert.pem";
    m_key_file              = m_dir / "key.pem";
    const auto& certificate = test_certificate_pem();
    std::ofstream(m_cert_file) << certificate.cert;
    std::ofstream(m_key_file) << certificate.key;
  }

  ~certificate_files()
  {
    std::error_code ignored;
    std::filesystem::remove_all(m_dir, ignored);
  }

  std::string cert_file() const
  {
    return encode_query_value(m_cert_file.string());
  }

  std::string key_file() const
  {
    return encode_query_value(m_key_file.string());
  }

 private:
  std::filesystem::path m_dir;
  std::filesystem::path m_cert_file;
  std::filesystem::path m_key_file;
};

static void check_tls_config_errors()
{
  const std::array<std::string, 7> invalid_queries = {
      "ssl&ssl.tlsv12&ssl.tlsv13&ssl.peer_verification=false&ssl.request_peer_cert=false",
      "ssl&ssl.cert=inline&ssl.cert_file=/missing/cert.pem&ssl.peer_verification=false&ssl.request_peer_cert=false",
      "ssl&ssl.cert=engine-cert&ssl.cert_type=engine&ssl.peer_verification=false&ssl.request_peer_cert=false",
      "ssl&ssl.key_type=bogus&ssl.peer_verification=false&ssl.request_peer_cert=false",
      "ssl&ssl.peer_verification=true&ssl.request_peer_cert=false",
      "ssl&ssl.groups=not-a-real-tls-group&ssl.peer_verification=false&ssl.request_peer_cert=false",
      "ssl&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.alpn_protos=" + std::string(256, 'a'),
  };

  for (const auto& query : invalid_queries) {
    boost::asio::io_context io_context;
    const auto endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(unused_tcp_port()) + "?" + query);
    rstream::io::stream::stream_socket socket(io_context.get_executor());
    bool completed = false;
    socket.async_connect(endpoint, [&](const boost::system::error_code& error) {
      assert_stream_error(error, rstream::io::detail::stream::error::code::ssl_configuration_error);
      completed = true;
    });
    io_context.run();
    assert(completed);
  }
}

static void check_direct_tls_context_configuration()
{
  {
    auto config     = base_ssl_config();
    config.m_tlsv12 = true;
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::client);
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::server);
  }
  {
    auto config     = base_ssl_config();
    config.m_tlsv13 = true;
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::client);
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::server);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_cert           = certificate.cert;
    config.m_cert_type      = "pem";
    config.m_key            = certificate.key;
    config.m_key_type       = "pem";
    config.m_passphrase     = "unused";
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::server);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_cert           = certificate.cert;
    config.m_cert_type      = "asn1";
    assert_direct_ssl_config_fails(config);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_cert           = certificate.cert;
    config.m_cert_type      = "unsupported";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config        = base_ssl_config();
    config.m_cert_type = "pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config         = base_ssl_config();
    config.m_passphrase = "unused";
    assert_direct_ssl_config_fails(config);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_key            = certificate.key;
    config.m_key_file       = "/missing/key.pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_key            = certificate.key;
    config.m_key_type       = "unsupported";
    assert_direct_ssl_config_fails(config);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_key            = certificate.key;
    config.m_key_type       = "engine";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config       = base_ssl_config();
    config.m_key_type = "pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config                 = base_ssl_config();
    config.m_tmp_dh_params      = "bad-dh";
    config.m_tmp_dh_params_file = "/missing/dh.pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config            = base_ssl_config();
    config.m_tmp_dh_params = "bad-dh";
    assert_direct_ssl_config_fails(config);
  }
  {
    const auto& certificate = test_certificate_pem();
    auto config             = base_ssl_config();
    config.m_cacert         = certificate.cert;
    config.m_cacert_file    = "/missing/ca.pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config     = base_ssl_config();
    config.m_cacert = "not-a-ca";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config          = base_ssl_config();
    config.m_cacert_file = "/missing/ca.pem";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config     = base_ssl_config();
    config.m_capath = "/missing/ca-dir";
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::client);
  }
  {
    auto config      = base_ssl_config();
    config.m_ciphers = "not-a-cipher";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config             = base_ssl_config();
    config.m_ciphers_tlsv13 = "not-a-tls13-cipher";
    assert_direct_ssl_config_fails(config);
  }
  {
    auto config         = base_ssl_config();
    config.m_ciphers    = "DEFAULT";
    config.m_groups     = "X25519:secp256r1";
    config.m_client_rpk = true;
    config.m_server_rpk = true;
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::client);
  }
  {
    auto config                = base_ssl_config();
    config.m_request_peer_cert = true;
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::server);
  }
  {
    auto config                = base_ssl_config();
    config.m_peer_verification = true;
    config.m_request_peer_cert = true;
    assert_direct_ssl_config_succeeds(config, rstream::io::detail::stream::stream_socket_ssl::type::server);
  }
  check_direct_tls_shutdown_path();
}

static void check_tls_accept_connect_and_transfer(const std::string& groups_query = "")
{
  certificate_files files;
  boost::asio::io_context io_context;
  const auto port            = unused_tcp_port();
  const auto server_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port) + "?ssl&ssl.cert_file=" + files.cert_file()
          + "&ssl.key_file=" + files.key_file()
          + "&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.async_shutdown_timeout_ms=0" + groups_query);
  const auto client_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port)
          + "?ssl&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.sni=localhost&ssl.alpn_protos=rstrm%2F1&ssl.async_shutdown_timeout_ms=0" + groups_query);

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(server_endpoint, error_code);
  assert(!error_code);
  acceptor.bind(server_endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);

  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(client_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  run_until(io_context, [&] { return accepted && connected; });
  assert(accepted);
  assert(connected);
  assert(client.is_secure());
  assert(server_peer.is_secure());

  std::array<char, 5> server_buffer{};
  const std::string client_message = "hello";
  bool server_read                 = false;
  bool client_sent                 = false;
  boost::asio::async_read(server_peer, boost::asio::buffer(server_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == server_buffer.size());
    assert(std::string(server_buffer.data(), server_buffer.size()) == "hello");
    server_read = true;
  });
  boost::asio::async_write(client, boost::asio::buffer(client_message), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 5);
    client_sent = true;
  });
  run_until(io_context, [&] { return server_read && client_sent; });
  assert(server_read);
  assert(client_sent);

  std::array<char, 5> client_buffer{};
  const std::string server_message = "world";
  bool client_read                 = false;
  bool server_sent                 = false;
  boost::asio::async_read(client, boost::asio::buffer(client_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == client_buffer.size());
    assert(std::string(client_buffer.data(), client_buffer.size()) == "world");
    client_read = true;
  });
  boost::asio::async_write(server_peer, boost::asio::buffer(server_message), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 5);
    server_sent = true;
  });
  run_until(io_context, [&] { return client_read && server_sent; });
  assert(client_read);
  assert(server_sent);

  std::array<char, 5> server_sequence_buffer{};
  const std::string first_part                          = "he";
  const std::string second_part                         = "llo";
  std::vector<boost::asio::const_buffer> client_buffers = {
      boost::asio::buffer(first_part),
      boost::asio::buffer(second_part),
  };
  bool server_sequence_read = false;
  bool client_sequence_sent = false;
  boost::asio::async_read(server_peer, boost::asio::buffer(server_sequence_buffer), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == server_sequence_buffer.size());
    assert(std::string(server_sequence_buffer.data(), server_sequence_buffer.size()) == "hello");
    server_sequence_read = true;
  });
  client.async_write_some(client_buffers, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == first_part.size() + second_part.size());
    client_sequence_sent = true;
  });
  run_until(io_context, [&] { return server_sequence_read && client_sequence_sent; });
  assert(server_sequence_read);
  assert(client_sequence_sent);

  std::array<char, 2> client_sequence_prefix{};
  std::array<char, 3> client_sequence_suffix{};
  std::vector<boost::asio::mutable_buffer> client_read_buffers = {
      boost::asio::buffer(client_sequence_prefix),
      boost::asio::buffer(client_sequence_suffix),
  };
  bool client_sequence_read = false;
  bool server_sequence_sent = false;
  boost::asio::async_write(server_peer, boost::asio::buffer(server_message), [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 5);
    server_sequence_sent = true;
  });
  boost::asio::async_read(client, client_read_buffers, [&](const boost::system::error_code& error, std::size_t size) {
    assert(!error);
    assert(size == 5);
    assert(std::string(client_sequence_prefix.data(), client_sequence_prefix.size()) + std::string(client_sequence_suffix.data(), client_sequence_suffix.size()) == "world");
    client_sequence_read = true;
  });
  run_until(io_context, [&] { return client_sequence_read && server_sequence_sent; });
  assert(client_sequence_read);
  assert(server_sequence_sent);
}

class tls_half_close_exchange : public std::enable_shared_from_this<tls_half_close_exchange> {
 public:
  tls_half_close_exchange(rstream::io::stream::stream_socket& client, rstream::io::stream::stream_socket& server)
      : m_client(client),
        m_server(server),
        m_payload(1024 * 1024, '\0'),
        m_response("response-after-client-eof"),
        m_client_response(m_response.size(), '\0')
  {
    for (std::size_t i = 0; i < m_payload.size(); ++i) {
      m_payload[i] = static_cast<char>((i * 31U + 17U) % 251U);
    }
  }

  void run()
  {
    read_server();
    boost::asio::async_read(
        m_client,
        boost::asio::buffer(m_client_response),
        [self = shared_from_this()](const boost::system::error_code& error_code, std::size_t size) {
          self->m_client_read_error = error_code;
          self->m_client_read_size  = size;
          self->m_client_read_done  = true;
          self->mark_completed(g_client_read_completed);
        });
    boost::asio::async_write(
        m_client,
        boost::asio::buffer(m_payload),
        [self = shared_from_this()](const boost::system::error_code& error_code, std::size_t size) {
          self->m_client_write_error = error_code;
          self->m_client_write_size  = size;
          self->m_client_write_done  = true;
          self->mark_completed(g_client_write_completed);
          self->m_client.async_shutdown_send(
              [self](const boost::system::error_code& shutdown_error) {
                self->on_shutdown_send(shutdown_error);
              });
        });
  }

  bool completed() const
  {
    return m_completion_mask.load(std::memory_order_acquire) == g_all_completed;
  }

  bool wait_for_completion(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(m_completion_mutex);
    return m_completion_condition.wait_for(lock, timeout, [this] { return completed(); });
  }

  void assert_success() const
  {
    assert(!m_client_write_error);
    assert(m_client_write_size == m_payload.size());
    assert(m_server_read_error == boost::asio::error::eof);
    assert(m_server_payload == m_payload);
    assert(!m_server_write_error);
    assert(m_server_write_size == m_response.size());
    assert(!m_client_read_error);
    assert(m_client_read_size == m_response.size());
    assert(m_client_response == m_response);
    assert(!m_first_shutdown_error);
    assert(!m_second_shutdown_error);
    assert(m_shutdown_count == 2);
  }

 private:
  void read_server()
  {
    m_server.async_read_some(
        boost::asio::buffer(m_server_buffer),
        [self = shared_from_this()](const boost::system::error_code& error_code, std::size_t size) {
          self->m_server_payload.append(self->m_server_buffer.data(), size);
          if (!error_code) {
            self->read_server();
            return;
          }
          self->m_server_read_error = error_code;
          self->m_server_read_done  = true;
          self->mark_completed(g_server_read_completed);
          if (error_code == boost::asio::error::eof) {
            boost::asio::async_write(
                self->m_server,
                boost::asio::buffer(self->m_response),
                [self](const boost::system::error_code& write_error, std::size_t write_size) {
                  self->m_server_write_error = write_error;
                  self->m_server_write_size  = write_size;
                  self->m_server_write_done  = true;
                  self->mark_completed(g_server_write_completed);
                });
          }
          else {
            self->m_server_write_done = true;
            self->mark_completed(g_server_write_completed);
          }
        });
  }

  void on_shutdown_send(const boost::system::error_code& error_code)
  {
    ++m_shutdown_count;
    if (m_shutdown_count == 1) {
      m_first_shutdown_error = error_code;
      m_client.async_shutdown_send(
          [self = shared_from_this()](const boost::system::error_code& repeated_error) {
            self->on_shutdown_send(repeated_error);
          });
    }
    else {
      m_second_shutdown_error = error_code;
      mark_completed(g_shutdown_completed);
    }
  }

  void mark_completed(unsigned int bit)
  {
    const auto previous = m_completion_mask.fetch_or(bit, std::memory_order_acq_rel);
    assert((previous & bit) == 0);
    if ((previous | bit) == g_all_completed) {
      std::lock_guard lock(m_completion_mutex);
      m_completion_condition.notify_all();
    }
  }

  static constexpr unsigned int g_client_write_completed = 1U << 0U;
  static constexpr unsigned int g_shutdown_completed     = 1U << 1U;
  static constexpr unsigned int g_server_read_completed  = 1U << 2U;
  static constexpr unsigned int g_server_write_completed = 1U << 3U;
  static constexpr unsigned int g_client_read_completed  = 1U << 4U;
  static constexpr unsigned int g_all_completed          = g_client_write_completed | g_shutdown_completed | g_server_read_completed | g_server_write_completed | g_client_read_completed;

  rstream::io::stream::stream_socket& m_client;
  rstream::io::stream::stream_socket& m_server;
  std::string m_payload;
  std::string m_response;
  std::string m_client_response;
  std::string m_server_payload;
  std::array<char, 32 * 1024> m_server_buffer{};
  boost::system::error_code m_client_write_error;
  boost::system::error_code m_server_read_error;
  boost::system::error_code m_server_write_error;
  boost::system::error_code m_client_read_error;
  boost::system::error_code m_first_shutdown_error;
  boost::system::error_code m_second_shutdown_error;
  std::size_t m_client_write_size = 0;
  std::size_t m_server_write_size = 0;
  std::size_t m_client_read_size  = 0;
  unsigned int m_shutdown_count   = 0;
  std::atomic_uint m_completion_mask = 0;
  std::mutex m_completion_mutex;
  std::condition_variable m_completion_condition;
  bool m_client_write_done        = false;
  bool m_server_read_done         = false;
  bool m_server_write_done        = false;
  bool m_client_read_done         = false;
};

static void check_tls_half_close_preserves_receive_direction()
{
  certificate_files files;
  boost::asio::io_context io_context;
  const auto port            = unused_tcp_port();
  const auto server_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port) + "?ssl&ssl.cert_file=" + files.cert_file()
          + "&ssl.key_file=" + files.key_file()
          + "&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.async_shutdown_timeout_ms=0");
  const auto client_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port)
          + "?ssl&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.sni=localhost&ssl.async_shutdown_timeout_ms=0");
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(server_endpoint, error_code);
  assert(!error_code);
  acceptor.bind(server_endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket server(io_context.get_executor());
  rstream::io::stream::stream_socket client(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(client_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  run_until(io_context, [&] { return accepted && connected; });
  auto exchange = std::make_shared<tls_half_close_exchange>(client, server);
  exchange->run();
  auto work = boost::asio::make_work_guard(io_context);
  std::array<std::thread, 4> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] { io_context.run(); });
  }
  const auto completed = exchange->wait_for_completion(std::chrono::seconds(10));
  boost::system::error_code ignored;
  client.close(ignored);
  server.close(ignored);
  acceptor.close(ignored);
  work.reset();
  if (!completed) {
    io_context.stop();
  }
  for (auto& worker : workers) {
    worker.join();
  }
  assert(completed);
  exchange->assert_success();
}

static void check_tls_shutdown_timeout_is_serialized()
{
  certificate_files files;
  boost::asio::io_context io_context;
  const auto port            = unused_tcp_port();
  const auto server_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port) + "?ssl&ssl.cert_file=" + files.cert_file()
          + "&ssl.key_file=" + files.key_file()
          + "&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.async_shutdown_timeout_ms=0");
  const auto client_endpoint = resolve_one(io_context, "tcp://127.0.0.1:" + std::to_string(port));
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(server_endpoint, error_code);
  assert(!error_code);
  acceptor.bind(server_endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  client.open(client_endpoint, error_code);
  assert(!error_code);
  auto client_config                        = base_ssl_config();
  client_config.m_sni                       = "localhost";
  client_config.m_async_shutdown_timeout_ms = 25;
  auto client_ssl                           = rstream::io::detail::stream::stream_socket_ssl::wrap(
      client,
      client_config,
      rstream::io::detail::stream::stream_socket_ssl::type::client);
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(client_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  run_until(io_context, [&] { return accepted && connected; });
  std::atomic<unsigned int> completion_count = 0;
  boost::system::error_code shutdown_error;
  client_ssl->async_shutdown([&](const boost::system::error_code& error) {
    shutdown_error = error;
    completion_count.fetch_add(1, std::memory_order_relaxed);
  });
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (unsigned int i = 0; i < 4; ++i) {
    workers.emplace_back([&] { io_context.run(); });
  }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (completion_count.load(std::memory_order_relaxed) == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  io_context.stop();
  for (auto& worker : workers) {
    worker.join();
  }
  assert(completion_count.load(std::memory_order_relaxed) == 1);
  assert(shutdown_error == rstream::io::detail::stream::error::code::operation_aborted);
  boost::system::error_code ignored;
  server_peer.close(ignored);
  acceptor.close(ignored);
}

static void check_tls_accept_preserves_peer_executor()
{
  certificate_files files;
  boost::asio::io_context io_context;
  boost::asio::io_context peer_io_context;
  const auto port            = unused_tcp_port();
  const auto server_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port) + "?ssl&ssl.cert_file=" + files.cert_file()
          + "&ssl.key_file=" + files.key_file()
          + "&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.async_shutdown_timeout_ms=0");
  const auto client_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port)
          + "?ssl&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.sni=localhost&ssl.async_shutdown_timeout_ms=0");
  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(server_endpoint, error_code);
  assert(!error_code);
  acceptor.bind(server_endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);
  rstream::io::stream::stream_socket server_peer(peer_io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    accepted = true;
  });
  client.async_connect(client_endpoint, [&](const boost::system::error_code& error) {
    assert(!error);
    connected = true;
  });
  run_until(io_context, [&] { return accepted && connected; });
  assert(server_peer.get_executor() == peer_io_context.get_executor());
}

static boost::system::error_code run_verified_tls_connect(const std::string& sni)
{
  certificate_files files;
  boost::asio::io_context io_context;
  const auto port            = unused_tcp_port();
  const auto server_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port) + "?ssl&ssl.cert_file=" + files.cert_file()
          + "&ssl.key_file=" + files.key_file()
          + "&ssl.peer_verification=false&ssl.request_peer_cert=false&ssl.async_shutdown_timeout_ms=0");
  const auto client_endpoint = resolve_one(
      io_context,
      "tcp://127.0.0.1:" + std::to_string(port)
          + "?ssl&ssl.peer_verification=true&ssl.request_peer_cert=true&ssl.cacert_file=" + files.cert_file()
          + "&ssl.sni=" + encode_query_value(sni) + "&ssl.async_shutdown_timeout_ms=0");

  rstream::io::stream::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.open(server_endpoint, error_code);
  assert(!error_code);
  acceptor.bind(server_endpoint, error_code);
  assert(!error_code);
  acceptor.listen(boost::asio::socket_base::max_listen_connections, error_code);
  assert(!error_code);

  rstream::io::stream::stream_socket server_peer(io_context.get_executor());
  rstream::io::stream::endpoint remote_endpoint;
  rstream::io::stream::stream_socket client(io_context.get_executor());
  boost::system::error_code server_error;
  boost::system::error_code client_error;
  bool accepted  = false;
  bool connected = false;
  acceptor.async_accept(server_peer, remote_endpoint, [&](const boost::system::error_code& error) {
    server_error = error;
    accepted     = true;
  });
  client.async_connect(client_endpoint, [&](const boost::system::error_code& error) {
    client_error = error;
    connected    = true;
  });
  run_until(io_context, [&] { return connected; });
  boost::system::error_code ignored;
  client.close(ignored);
  server_peer.close(ignored);
  acceptor.close(ignored);
  io_context.run_for(std::chrono::milliseconds(100));
  if (!client_error) {
    assert(accepted);
  }
  (void)server_error;
  return client_error;
}

static void check_tls_peer_verification_checks_hostname()
{
  auto valid = run_verified_tls_connect("localhost");
  if (valid) {
    std::cerr << "verified TLS connection unexpectedly failed: " << valid.category().name()
              << ":" << valid.value() << " " << valid.message() << std::endl;
  }
  assert(!valid);

  auto invalid = run_verified_tls_connect("wrong.localhost");
  assert(invalid);
}

#if OPENSSL_VERSION_NUMBER >= 0x30500000L
static void check_tls_hybrid_group_preferences()
{
  check_tls_accept_connect_and_transfer("&ssl.groups=SecP256r1MLKEM768");
}
#endif

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_tls_config_errors();
  check_direct_tls_context_configuration();
  check_tls_accept_connect_and_transfer();
  check_tls_half_close_preserves_receive_direction();
  check_tls_shutdown_timeout_is_serialized();
  check_tls_accept_preserves_peer_executor();
  check_tls_peer_verification_checks_hostname();
#if OPENSSL_VERSION_NUMBER >= 0x30500000L
  check_tls_hybrid_group_preferences();
#endif
  return 0;
}
