// See LICENSE file in the project root for license information.

#include "stream_socket_ssl.hpp"

#include <openssl/opensslv.h>

#define SSL_STREAM_PRINT_PEER_PKEY    1
#define SSL_STREAM_USE_OPENSSL_ENGINE 1
#define SSL_STREAM_USE_RPK            OPENSSL_VERSION_NUMBER >= 0x30200000L
#define SSL_STREAM_USE_STRAND         1

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
#include <openssl/engine.h>
#include <openssl/ui.h>
#endif

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/strand.hpp>

#include <fmt/ranges.h>
#include <openssl/x509_vfy.h>

#include <rstream/config.hpp>
#include <rstream/core/allocator.hpp>
#include <rstream/core/completion_handler.hpp>
#include <rstream/core/log.hpp>

#include "error.hpp"

#if !defined(FALLTHROUGH)
#if (defined(__GNUC__) && __GNUC__ >= 7) || (defined(__clang__) && __clang_major__ >= 10)
#define FALLTHROUGH() __attribute__((fallthrough))
#else
#define FALLTHROUGH() \
  do {                \
  } while (0)
#endif
#endif

namespace rstream {
namespace io {
namespace detail {
namespace stream {

static boost::system::error_code translate_error(long error);

static boost::system::error_code configure_expected_peer_identity(SSL* ssl, const std::string& identity);

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
static int ssl_ui_reader(UI* ui, UI_STRING* uis);
static int ssl_ui_writer(UI* ui, UI_STRING* uis);
#endif

static const unsigned long g_async_shutdown_timeout_ms = 5000;

static boost::system::error_code configure_expected_peer_identity(SSL* ssl, const std::string& identity)
{
  if (identity.empty()) {
    return {};
  }
  ::ERR_clear_error();
  boost::system::error_code address_error;
  (void)boost::asio::ip::make_address(identity, address_error);
  int ok = 0;
  if (!address_error) {
    ok = ::X509_VERIFY_PARAM_set1_ip_asc(::SSL_get0_param(ssl), identity.c_str());
  }
  else {
    ok = ::SSL_set1_host(ssl, identity.c_str());
  }
  if (ok == 1) {
    return {};
  }
  auto error_code = translate_error(::ERR_get_error());
  if (!error_code) {
    error_code = error::make_error_code(error::code::ssl_configuration_error);
  }
  return error_code;
}

class RSTREAM_GNUC_INTERNAL stream_socket_ssl::impl : public std::enable_shared_from_this<impl> {
 public:
  using ptr = std::shared_ptr<impl>;

  impl(stream_socket_ptr next_layer, const ssl::config& config, type type, core::allocator::ptr allocator);

  virtual ~impl();

  void open(const endpoint& endpoint, boost::system::error_code& error_code);

  void close(boost::system::error_code& error_code);

  endpoint remote_endpoint(boost::system::error_code& error_code);

  void async_handshake(async_handshake_completion_handler&& handler);

  void async_shutdown(async_shutdown_completion_handler&& handler);

  void async_connect(const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler);

  void async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler);

  void async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler);

  stream_socket_const_ptr next_layer() const;

  stream_socket_ptr next_layer();

 private:
#if SSL_STREAM_USE_STRAND == 1
  void async_handshake_internal(async_handshake_completion_handler&& handler);

  void async_shutdown_internal(async_shutdown_completion_handler&& handler);

  void async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler);

  void async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler);

  void async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler);

  void async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler);

  void async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler);
#endif

  boost::asio::ssl::context make_ssl_context();

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
  void init_engine(const std::string& engine, boost::system::error_code& error_code);
#endif

  using ssl_stream_type = boost::asio::ssl::stream<stream_socket_interface&>;

  class async_connect_operation;

  friend class async_connect_operation;

  class async_shutdown_operation;

  friend class async_shutdown_operation;

#if SSL_STREAM_USE_STRAND == 1
  boost::asio::strand<executor_type> m_strand;
#endif

  core::allocator::ptr m_allocator;

  rstream::core::logger m_logger;

  stream_socket_ptr m_next_layer;

  const ssl::config m_config;

  type m_type;

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
  ENGINE* m_engine;
#endif

  boost::asio::ssl::context m_ssl_context;

  ssl_stream_type m_ssl_stream;
};

class stream_socket_ssl::impl::async_connect_operation : public std::enable_shared_from_this<async_connect_operation> {
 public:
  async_connect_operation(ptr ptr, const endpoint& endpoint, async_connect_completion_handler&& handler);

  void run();

  void do_connect();

  void on_connect(const boost::system::error_code& error_code);

  void do_handshake();

  void on_handshake(const boost::system::error_code& error_code);

  void on_complete(const boost::system::error_code& error_code);

 private:
  ptr m_ptr;

  endpoint m_endpoint;

  rstream::core::logger m_logger;

  async_connect_completion_handler m_handler;
};

class stream_socket_ssl::impl::async_shutdown_operation : public std::enable_shared_from_this<async_shutdown_operation> {
 public:
  async_shutdown_operation(ptr ptr, async_shutdown_completion_handler&& handler);

  void run();

  void arm_timer(unsigned long timeout_ms);

  void do_shutdown();

  void on_timer_cb(const boost::system::error_code& error_code);

  void on_shutdown(const boost::system::error_code& error_code);

  void on_complete(const boost::system::error_code& error_code);

 private:
  ptr m_ptr;

  boost::asio::strand<executor_type> m_strand;

  rstream::core::logger m_logger;

  bool m_complete;

  boost::asio::deadline_timer m_timer;

  async_handshake_completion_handler m_handler;
};

stream_socket_ssl::stream_socket_ssl(stream_socket_ptr next_layer, const ssl::config& config, type type)
    : stream_socket_base(next_layer->get_executor()),
      m_impl(std::make_shared<impl>(next_layer, config, type, nullptr))
{
}

void stream_socket_ssl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_impl->open(endpoint, error_code);
}

void stream_socket_ssl::close(boost::system::error_code& error_code)
{
  m_impl->close(error_code);
}

endpoint stream_socket_ssl::remote_endpoint(boost::system::error_code& error_code)
{
  return m_impl->remote_endpoint(error_code);
}

bool stream_socket_ssl::is_secure() const
{
  return true;
}

stream_socket_const_ptr stream_socket_ssl::next_layer() const
{
  return m_impl->next_layer();
}

stream_socket_ptr stream_socket_ssl::next_layer()
{
  return m_impl->next_layer();
}

void stream_socket_ssl::async_handshake(async_handshake_completion_handler&& handler)
{
  m_impl->async_handshake(std::move(handler));
}

void stream_socket_ssl::async_shutdown(async_shutdown_completion_handler&& handler)
{
  m_impl->async_shutdown(std::move(handler));
}

std::shared_ptr<stream_socket_ssl> stream_socket_ssl::wrap(stream_socket& peer, const ssl::config& config, type type)
{
  auto stream_socket_ssl_ptr = std::make_shared<stream_socket_ssl>(peer.native_handle(), config, type);
  peer.swap(stream_socket_ssl_ptr);
  return stream_socket_ssl_ptr;
}

void stream_socket_ssl::async_connect_internal(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  m_impl->async_connect(endpoint, std::move(handler));
}

void stream_socket_ssl::async_write_some_internal(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  m_impl->async_write_some(buffer, std::move(handler));
}

void stream_socket_ssl::async_write_some_internal(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  m_impl->async_write_some(buffer, std::move(handler));
}

void stream_socket_ssl::async_read_some_internal(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  m_impl->async_read_some(buffer, std::move(handler));
}

void stream_socket_ssl::async_read_some_internal(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  m_impl->async_read_some(buffer, std::move(handler));
}

stream_socket_ssl::impl::impl(stream_socket_ptr next_layer, const ssl::config& config, type type, core::allocator::ptr allocator)
    :
#if SSL_STREAM_USE_STRAND == 1
      m_strand(next_layer->get_executor()),
#endif
      m_allocator(allocator),
      m_logger({"rstream", "io", "stream", "socket", fmt::format("#{}", fmt::ptr(this))}),
      m_next_layer(next_layer),
      m_config(config),
      m_type(type),
#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
      m_engine(nullptr),
#endif
      m_ssl_context(make_ssl_context()),
      m_ssl_stream(*next_layer, m_ssl_context)
{
}

stream_socket_ssl::impl::~impl()
{
#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
  if (m_engine) {
    ::ENGINE_finish(m_engine);
    ::ENGINE_free(m_engine);
    m_engine = nullptr;
  }
#endif
}

void stream_socket_ssl::impl::open(const endpoint& endpoint, boost::system::error_code& error_code)
{
  m_next_layer->open(endpoint, error_code);
}

void stream_socket_ssl::impl::close(boost::system::error_code& error_code)
{
  (void)error_code;
  auto handler = [ptr = shared_from_this()](const boost::system::error_code& error_code) {
    (void)error_code;
    boost::system::error_code tmp;
    ptr->m_next_layer->close(tmp);
  };
  async_shutdown(handler);
}

endpoint stream_socket_ssl::impl::remote_endpoint(boost::system::error_code& error_code)
{
  return m_next_layer->remote_endpoint(error_code);
}

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_handshake(async_handshake_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_handshake_internal, shared_from_this(), std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_shutdown(async_shutdown_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_shutdown_internal, shared_from_this(), std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_connect(const endpoint& endpoint, async_connect_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front(&impl::async_connect_internal, shared_from_this(), endpoint, std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_write_some(const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const boost::asio::const_buffer&, async_write_some_completion_handler&&)) & impl::async_write_some_internal, shared_from_this(), buffer, std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_write_some(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const const_buffer_sequence_type& buffer, async_write_some_completion_handler&&)) & impl::async_write_some_internal, shared_from_this(), buffer, std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_read_some(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&&)) & impl::async_read_some_internal, shared_from_this(), buffer, std::move(handler)));
}
#endif

#if SSL_STREAM_USE_STRAND == 1
void stream_socket_ssl::impl::async_read_some(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
  boost::asio::dispatch(m_strand, std::bind_front((void(impl::*)(const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&&)) & impl::async_read_some_internal, shared_from_this(), buffer, std::move(handler)));
}
#endif

stream_socket_const_ptr stream_socket_ssl::impl::next_layer() const
{
  return m_next_layer;
}

stream_socket_ptr stream_socket_ssl::impl::next_layer()
{
  return m_next_layer;
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_handshake_internal
#else
    async_handshake
#endif
    (async_handshake_completion_handler&& handler)
{
  boost::asio::ssl::stream_base::handshake_type handshake_type;
  if (m_type == type::client) {
    handshake_type = boost::asio::ssl::stream_base::client;
  }
  else {
    handshake_type = boost::asio::ssl::stream_base::server;
  }
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  m_ssl_stream.async_handshake(handshake_type, std::move(handler));
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_shutdown_internal
#else
    async_shutdown
#endif
    (async_shutdown_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  std::allocate_shared<async_shutdown_operation>(core::allocator::wrapper<async_shutdown_operation>(m_allocator), shared_from_this(), std::forward<decltype(handler)>(handler))->run();
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_connect_internal
#else
    async_connect
#endif
    (const endpoint& endpoint, async_connect_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  std::allocate_shared<async_connect_operation>(core::allocator::wrapper<async_connect_operation>(m_allocator), shared_from_this(), endpoint, std::forward<decltype(handler)>(handler))->run();
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_write_some_internal
#else
    async_write_some
#endif
    (const boost::asio::const_buffer& buffer, async_write_some_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  m_ssl_stream.async_write_some(buffer, std::move(handler));
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_write_some_internal
#else
    async_write_some
#endif
    (const const_buffer_sequence_type& buffer, async_write_some_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  m_ssl_stream.async_write_some(buffer, std::move(handler));
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_read_some_internal
#else
    async_read_some
#endif
    (const boost::asio::mutable_buffer& buffer, async_read_some_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  m_ssl_stream.async_read_some(buffer, std::move(handler));
}

void stream_socket_ssl::impl::
#if SSL_STREAM_USE_STRAND == 1
    async_read_some_internal
#else
    async_read_some
#endif
    (const mutable_buffer_sequence_type& buffer, async_read_some_completion_handler&& handler)
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#endif
  m_ssl_stream.async_read_some(buffer, std::move(handler));
}

boost::asio::ssl::context stream_socket_ssl::impl::make_ssl_context()
{
  boost::system::error_code error_code;
  boost::asio::ssl::context_base::method method;
  if (m_config.m_tlsv12 || m_config.m_tlsv13) {
    if (m_config.m_tlsv12 && m_config.m_tlsv13) {
#ifdef DEBUG_BUILD
      m_logger->warn("both TLSv1.2 and TLSv1.3 are specified");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    if (m_config.m_tlsv12) {
      if (m_type == type::client) {
        method = boost::asio::ssl::context_base::method::tlsv12_client;
      }
      else {
        method = boost::asio::ssl::context_base::method::tlsv12_server;
      }
    }
    else if (m_config.m_tlsv13) {
      if (m_type == type::client) {
        method = boost::asio::ssl::context_base::method::tlsv13_client;
      }
      else {
        method = boost::asio::ssl::context_base::method::tlsv13_server;
      }
    }
  }
  else {
    if (m_type == type::client) {
      method = boost::asio::ssl::context_base::method::tls_client;
    }
    else {
      method = boost::asio::ssl::context_base::method::tls_server;
    }
  }
  boost::asio::ssl::context ssl_context(method);
  ssl_context.set_options(boost::asio::ssl::context::default_workarounds | boost::asio::ssl::context::single_dh_use, error_code);
  if (error_code) {
#ifdef DEBUG_BUILD
    m_logger->warn("failed to set SSL context options [error_code: {}]", error_code.message());
#endif
    throw boost::system::system_error(error_code);
  }
  if (m_config.m_client_rpk) {
#if SSL_STREAM_USE_RPK == 1
    unsigned char cert_type[] = {TLSEXT_cert_type_rpk, TLSEXT_cert_type_x509};
    ::ERR_clear_error();
    if (::SSL_CTX_set1_client_cert_type(ssl_context.native_handle(), cert_type, sizeof(cert_type)) != 1) {
      error_code = translate_error(::ERR_get_error());
    }
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to enable raw public keys functionality (RPK) (RFC7250) for client [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->trace("raw public keys functionality (RPK) (RFC7250) enabled for client");
#endif
    }
#else
#ifdef DEBUG_BUILD
    m_logger->warn("raw public keys functionality (RPK) (RFC7250) is not supported");
#endif
#endif
  }
  if (m_config.m_server_rpk) {
#if SSL_STREAM_USE_RPK == 1
    unsigned char cert_type[] = {TLSEXT_cert_type_rpk, TLSEXT_cert_type_x509};
    ::ERR_clear_error();
    if (::SSL_CTX_set1_server_cert_type(ssl_context.native_handle(), cert_type, sizeof(cert_type)) != 1) {
      error_code = translate_error(::ERR_get_error());
    }
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to enable raw public keys functionality (RPK) (RFC7250) for server [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->trace("raw public keys functionality (RPK) (RFC7250) enabled for server");
#endif
    }
#else
#ifdef DEBUG_BUILD
    m_logger->warn("raw public keys functionality (RPK) (RFC7250) is not supported");
#endif
#endif
  }
  if (m_config.m_cert || m_config.m_cert_file) {
    if (m_config.m_cert && m_config.m_cert_file) {
#ifdef DEBUG_BUILD
      m_logger->warn("both SSL certificate and certificate file are specified");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    boost::optional<boost::asio::ssl::context::file_format> file_format;
    boost::optional<std::string> engine;
    if (m_config.m_cert_type) {
      if (m_config.m_cert_type.get() == "pem") {
        file_format = boost::asio::ssl::context::pem;
      }
      else if (m_config.m_cert_type.get() == "asn1") {
        file_format = boost::asio::ssl::context::asn1;
      }
      else if (m_config.m_cert_type.get() == "engine") {
        if (m_config.m_cert_file) {
#ifdef DEBUG_BUILD
          m_logger->warn("cannot pass SSL certificate as file when using SSL engine");
#endif
          throw boost::system::system_error(error::code::ssl_configuration_error);
        }
        if (m_config.m_engine) {
          engine = m_config.m_engine.get();
        }
        else {
#ifdef DEBUG_BUILD
          m_logger->warn("SSL engine is not specified");
#endif
          throw boost::system::system_error(error::code::ssl_configuration_error);
        }
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->warn("unsupported SSL certificate type: {}", m_config.m_cert_type.get());
#endif
        throw boost::system::system_error(error::code::ssl_configuration_error);
      }
    }
    else {
      file_format = boost::asio::ssl::context::pem;
    }
    if (file_format) {
      if (m_config.m_cert) {
        ssl_context.use_certificate(boost::asio::const_buffer(m_config.m_cert.get().data(), m_config.m_cert.get().size()), file_format.get(), error_code);
        if (error_code) {
#ifdef DEBUG_BUILD
          m_logger->warn("failed to set SSL certificate [error_code: {}]", error_code.message());
#endif
          throw boost::system::system_error(error_code);
        }
      }
      else {
        ssl_context.use_certificate_file(m_config.m_cert_file.get(), file_format.get(), error_code);
        if (error_code) {
#ifdef DEBUG_BUILD
          m_logger->warn("failed to set SSL certificate file [error_code: {}]", error_code.message());
#endif
          throw boost::system::system_error(error_code);
        }
      }
    }
    else if (engine) {
#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
#ifdef ENGINE_CTRL_GET_CMD_FROM_NAME
      init_engine(engine.get(), error_code);
      if (error_code) {
        throw boost::system::system_error(error_code);
      }
      const char* cmd_name = "LOAD_CERT_CTRL";
      if (!ENGINE_ctrl(m_engine, ENGINE_CTRL_GET_CMD_FROM_NAME, 0, (void*)cmd_name, NULL)) {
#ifdef DEBUG_BUILD
        m_logger->warn("SSL engine does not support loading certificates");
#endif
        throw boost::system::system_error(error::code::ssl_configuration_error);
      }
      struct {
        const char* cert_id;
        X509* cert;
      } params;
      params.cert_id = m_config.m_cert.get().c_str();
      params.cert    = nullptr;
      if (!ENGINE_ctrl_cmd(m_engine, cmd_name, 0, &params, NULL, 1)) {
        error_code = translate_error(::ERR_get_error());
      }
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to load certificate from SSL engine [cert_id: {}, error_code: {}]", m_config.m_cert.get(), error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
      if (!params.cert) {
#ifdef DEBUG_BUILD
        m_logger->warn("SSL engine did not properly initialize the certificate");
#endif
        throw boost::system::system_error(error::code::ssl_configuration_error);
      }
      if (SSL_CTX_use_certificate(ssl_context.native_handle(), params.cert) != 1) {
        error_code = translate_error(::ERR_get_error());
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->trace("SSL certificate successfully loaded from SSL engine");
#endif
      }
      X509_free(params.cert);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL certificate from SSL engine [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
#else
#ifdef DEBUG_BUILD
      m_logger->warn("SSL engine does not support loading certificates");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
#endif
#else
#ifdef DEBUG_BUILD
      m_logger->warn("SSL engine is not supported");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
#endif
    }
  }
  else if (m_config.m_cert_type) {
#ifdef DEBUG_BUILD
    m_logger->warn("SSL certificate type is specified without SSL certificate");
#endif
    throw boost::system::system_error(error::code::ssl_configuration_error);
  }
  else if (m_config.m_passphrase) {
#ifdef DEBUG_BUILD
    m_logger->warn("passphrase should only be used with SSL private key");
#endif
    throw boost::system::system_error(error::code::ssl_configuration_error);
  }
  if (m_config.m_key || m_config.m_key_file) {
    if (m_config.m_key && m_config.m_key_file) {
#ifdef DEBUG_BUILD
      m_logger->warn("both SSL key and key file are specified");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    boost::optional<boost::asio::ssl::context::file_format> file_format;
    boost::optional<std::string> engine;
    if (m_config.m_key_type) {
      if (m_config.m_key_type.get() == "pem") {
        file_format = boost::asio::ssl::context::pem;
      }
      else if (m_config.m_key_type.get() == "asn1") {
        file_format = boost::asio::ssl::context::asn1;
      }
      else if (m_config.m_key_type.get() == "engine") {
        if (m_config.m_key_file) {
#ifdef DEBUG_BUILD
          m_logger->warn("cannot pass SSL key as file when using SSL engine");
#endif
          throw boost::system::system_error(error::code::ssl_configuration_error);
        }
        if (m_config.m_engine) {
          engine = m_config.m_engine.get();
        }
        else {
#ifdef DEBUG_BUILD
          m_logger->warn("SSL engine is not specified");
#endif
          throw boost::system::system_error(error::code::ssl_configuration_error);
        }
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->warn("unsupported SSL key type: {}", m_config.m_key_type.get());
#endif
        throw boost::system::system_error(error::code::ssl_configuration_error);
      }
    }
    else {
      file_format = boost::asio::ssl::context::pem;
    }
    if (file_format) {
      if (m_config.m_key) {
        ssl_context.use_private_key(boost::asio::const_buffer(m_config.m_key.get().data(), m_config.m_key.get().size()), file_format.get(), error_code);
        if (error_code) {
#ifdef DEBUG_BUILD
          m_logger->warn("failed to set SSL private key [error_code: {}]", error_code.message());
#endif
          throw boost::system::system_error(error_code);
        }
      }
      else {
        ssl_context.use_private_key_file(m_config.m_key_file.get(), file_format.get(), error_code);
        if (error_code) {
#ifdef DEBUG_BUILD
          m_logger->warn("failed to set SSL private key file [error_code: {}]", error_code.message());
#endif
          throw boost::system::system_error(error_code);
        }
      }
      if (file_format.get() == boost::asio::ssl::context::pem) {
        if (m_config.m_passphrase) {
          ssl_context.set_password_callback(
              [passphrase = m_config.m_passphrase.get()](std::size_t max_length, boost::asio::ssl::context::password_purpose purpose) {
                (void)max_length;
                (void)purpose;
                return passphrase;
              },
              error_code);
          if (error_code) {
#ifdef DEBUG_BUILD
            m_logger->warn("failed to set SSL passphrase [error_code: {}]", error_code.message());
#endif
            throw boost::system::system_error(error_code);
          }
        }
      }
      else if (m_config.m_passphrase) {
#ifdef DEBUG_BUILD
        m_logger->warn("passphrase is not supported for non-PEM SSL private key");
#endif
        throw boost::system::system_error(error::code::ssl_configuration_error);
      }
    }
    else if (engine) {
#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
      init_engine(engine.get(), error_code);
      if (error_code) {
        throw boost::system::system_error(error_code);
      }
      EVP_PKEY* private_key = nullptr;
      if (m_config.m_passphrase) {
        UI_METHOD* ui_method = UI_create_method((char*)"rstream-cpp user interface");
        if (!ui_method) {
#ifdef DEBUG_BUILD
          m_logger->warn("unable to create SSL user interface method");
#endif
          throw boost::system::system_error(error::code::ssl_configuration_error);
        }
        UI_method_set_opener(ui_method, UI_method_get_opener(UI_OpenSSL()));
        UI_method_set_closer(ui_method, UI_method_get_closer(UI_OpenSSL()));
        UI_method_set_reader(ui_method, ssl_ui_reader);
        UI_method_set_writer(ui_method, ssl_ui_writer);
        private_key = ENGINE_load_private_key(m_engine, m_config.m_key.get().c_str(), ui_method, (void*)m_config.m_passphrase.get().c_str());
        UI_destroy_method(ui_method);
      }
      else {
        private_key = ENGINE_load_private_key(m_engine, m_config.m_key.get().c_str(), nullptr, nullptr);
      }
      if (!private_key) {
        error_code = translate_error(::ERR_get_error());
      }
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("SSL engine did not properly initialize the private key [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
      if (SSL_CTX_use_PrivateKey(ssl_context.native_handle(), private_key) != 1) {
        error_code = translate_error(::ERR_get_error());
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->trace("SSL private key successfully loaded from SSL engine");
#endif
      }
      EVP_PKEY_free(private_key);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL private key from SSL engine [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
#else
#ifdef DEBUG_BUILD
      m_logger->warn("SSL engine is not supported");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
#endif
    }
  }
  else if (m_config.m_key_type) {
#ifdef DEBUG_BUILD
    m_logger->warn("SSL key type is specified without SSL key");
#endif
    throw boost::system::system_error(error::code::ssl_configuration_error);
  }
  if (m_config.m_tmp_dh_params || m_config.m_tmp_dh_params_file) {
    if (m_config.m_tmp_dh_params && m_config.m_tmp_dh_params_file) {
#ifdef DEBUG_BUILD
      m_logger->warn("both SSL temporary Diffie-Hellman parameters and temporary Diffie-Hellman parameters file are specified");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    if (m_config.m_tmp_dh_params) {
      ssl_context.use_tmp_dh(boost::asio::const_buffer(m_config.m_tmp_dh_params.get().data(), m_config.m_tmp_dh_params.get().size()), error_code);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL temporary Diffie-Hellman parameters [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
    }
    else {
      ssl_context.use_tmp_dh_file(m_config.m_tmp_dh_params_file.get(), error_code);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL temporary Diffie-Hellman parameters file [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
    }
  }
  if (m_config.m_cacert || m_config.m_cacert_file) {
    if (m_config.m_cacert && m_config.m_cacert_file) {
#ifdef DEBUG_BUILD
      m_logger->warn("both SSL CA certificate and CA certificate file are specified");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    if (m_config.m_cacert) {
      ssl_context.add_certificate_authority(boost::asio::const_buffer(m_config.m_cacert.get().data(), m_config.m_cacert.get().size()), error_code);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL CA certificate [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
    }
    else {
      ssl_context.load_verify_file(m_config.m_cacert_file.get(), error_code);
      if (error_code) {
#ifdef DEBUG_BUILD
        m_logger->warn("failed to set SSL CA certificate file [error_code: {}]", error_code.message());
#endif
        throw boost::system::system_error(error_code);
      }
    }
  }
  else if (!m_config.m_capath) {
#ifdef EMBED_DEFAULT_CA_CERTIFICATES
#ifdef DEBUG_BUILD
    m_logger->trace("using default SSL CA certificates from embedded data");
#endif
    ssl_context.add_certificate_authority(boost::asio::const_buffer(get_default_ca_certificates().data(), get_default_ca_certificates().size()), error_code);
#else
#ifdef DEBUG_BUILD
    m_logger->trace("using default SSL CA certificate from system");
#endif
    ssl_context.set_default_verify_paths(error_code);
#endif
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set default SSL CA certificates [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  if (m_config.m_capath) {
    ssl_context.add_verify_path(m_config.m_capath.get(), error_code);
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set SSL CA certificate path [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  if (m_config.m_ciphers) {
    ::ERR_clear_error();
    if (::SSL_CTX_set_cipher_list(ssl_context.native_handle(), m_config.m_ciphers.get().c_str()) != 1) {
      error_code = translate_error(::ERR_get_error());
    }
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set SSL ciphers suites [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  if (m_config.m_ciphers_tlsv13) {
    ::ERR_clear_error();
    if (::SSL_CTX_set_ciphersuites(ssl_context.native_handle(), m_config.m_ciphers_tlsv13.get().c_str()) != 1) {
      error_code = translate_error(::ERR_get_error());
    }
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set TLS 1.3 cipher suites [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  if (m_config.m_groups) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    ::ERR_clear_error();
    if (::SSL_CTX_set1_groups_list(ssl_context.native_handle(), m_config.m_groups.get().c_str()) != 1) {
      error_code = translate_error(::ERR_get_error());
      if (!error_code) {
        error_code = error::make_error_code(error::code::ssl_configuration_error);
      }
    }
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set TLS supported groups [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
#else
#ifdef DEBUG_BUILD
    m_logger->warn("TLS supported groups are not supported by this OpenSSL version");
#endif
    throw boost::system::system_error(error::code::ssl_configuration_error);
#endif
  }
  if (m_config.m_peer_verification || m_config.m_request_peer_cert) {
    if (m_config.m_peer_verification && !m_config.m_request_peer_cert) {
#ifdef DEBUG_BUILD
      m_logger->warn("peer verification is enabled without requesting peer certificate");
#endif
      throw boost::system::system_error(error::code::ssl_configuration_error);
    }
    int verify_mode = 0;
    if (m_config.m_request_peer_cert) {
      verify_mode |= boost::asio::ssl::verify_peer;
    }
    if (m_config.m_peer_verification) {
      verify_mode |= boost::asio::ssl::verify_fail_if_no_peer_cert;
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->trace("ignoring peer verification");
#endif
    }
    ssl_context.set_verify_mode(verify_mode);
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set SSL verify mode [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  {
    auto verify_certificate = [this](bool preverified, boost::asio::ssl::verify_context& context) {
#if SSL_STREAM_PRINT_PEER_PKEY == 1
      auto print_pkey = [this](EVP_PKEY* pkey, int peer_cert_type, const char* subject_name) {
        const char* key_type_name = ::EVP_PKEY_get0_type_name(pkey);
        BIO* bio                  = ::BIO_new(BIO_s_mem());
        if (::PEM_write_bio_PUBKEY(bio, pkey)) {
          char* pem_data;
          long pem_length = BIO_get_mem_data(bio, &pem_data);
          std::string str(pem_data, pem_length);
          if (str.length() > 0) {
            std::string::iterator it = str.end() - 1;
            if (*it == '\n') {
              str.erase(it);
            }
          }
          std::string peer_cert_type_str;
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
          if (peer_cert_type == TLSEXT_cert_type_x509)
#else
          if (peer_cert_type == 0)
#endif
          {
            peer_cert_type_str = "X.509";
          }
#if SSL_STREAM_USE_RPK == 1
          else if (peer_cert_type == TLSEXT_cert_type_rpk) {
            peer_cert_type_str = "RPK";
          }
#endif
          else {
            peer_cert_type_str = "unknown";
          }
#ifdef DEBUG_BUILD
          m_logger->trace("peer public key ({}) ({}){}\n{}", key_type_name, peer_cert_type_str, subject_name ? fmt::format(" ({})", subject_name) : "", str);
#endif
        }
        else {
#ifdef DEBUG_BUILD
          m_logger->warn("failed to write peer public key to BIO");
#endif
        }
        BIO_free(bio);
      };
#endif
      EVP_PKEY* pkey      = nullptr;
      char* subject_name  = nullptr;
      int peer_cert_type  = 0;
      X509_STORE_CTX* ctx = static_cast<X509_STORE_CTX*>(context.native_handle());
      int depth           = ::X509_STORE_CTX_get_error_depth(ctx);
      if (depth == 0) {
        X509* cert = ::X509_STORE_CTX_get_current_cert(ctx);
        if (cert) {
          pkey = ::X509_get_pubkey(cert);
          if (pkey) {
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
            peer_cert_type = TLSEXT_cert_type_x509;
#else
            peer_cert_type = 0;
#endif
            subject_name = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
          }
        }
        else {
#if SSL_STREAM_USE_RPK == 1
          pkey = ::X509_STORE_CTX_get0_rpk(ctx);
          if (pkey) {
            peer_cert_type = TLSEXT_cert_type_rpk;
          }
#endif
        }
        if (pkey) {
#if SSL_STREAM_PRINT_PEER_PKEY == 1
          print_pkey(pkey, peer_cert_type, subject_name);
#endif
        }
      }
      return m_config.m_peer_verification ? preverified : true;
    };
    ssl_context.set_verify_callback(verify_certificate, error_code);
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->warn("failed to set SSL verify callback [error_code: {}]", error_code.message());
#endif
      throw boost::system::system_error(error_code);
    }
  }
  return ssl_context;
}

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
void stream_socket_ssl::impl::init_engine(const std::string& engine, boost::system::error_code& error_code)
{
  if (m_engine) {
    return;
  }
  ::ERR_clear_error();
  ENGINE* native_engine = ::ENGINE_by_id(engine.c_str());
  if (!native_engine) {
    error_code = translate_error(::ERR_get_error());
  }
  if (error_code) {
#ifdef DEBUG_BUILD
    m_logger->warn("failed to load SSL engine [error_code: {}]", error_code.message());
#endif
    return;
  }
  if (::ENGINE_init(native_engine) != 1) {
    ENGINE_free(native_engine);
    error_code = translate_error(::ERR_get_error());
  }
  if (error_code) {
#ifdef DEBUG_BUILD
    m_logger->warn("failed to initialize SSL engine [error_code: {}]", error_code.message());
#endif
    return;
  }
  m_engine = native_engine;
}
#endif

stream_socket_ssl::impl::async_connect_operation::async_connect_operation(ptr ptr, const endpoint& endpoint, async_connect_completion_handler&& handler)
    : m_ptr(ptr),
      m_endpoint(endpoint),
      m_logger({"rstream", "io", "stream", "ssl", "connect", fmt::format("#{}", fmt::ptr(this))}),
      m_handler(std::move(handler))
{
}

void stream_socket_ssl::impl::async_connect_operation::run()
{
  do_connect();
}

void stream_socket_ssl::impl::async_connect_operation::do_connect()
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_ptr->m_strand.running_in_this_thread());
#endif
#endif
  boost::optional<std::string> sni;
  if (m_ptr->m_config.m_sni) {
    sni = m_ptr->m_config.m_sni.get();
  }
  else {
    const auto host = m_endpoint.get_url().host();
    if (!host.empty()) {
      sni = host;
    }
  }
  boost::system::error_code error_code;
  if (sni) {
#ifdef DEBUG_BUILD
    m_logger->trace("setting SNI extension to '{}'", sni.get());
#endif
    if (!SSL_set_tlsext_host_name(m_ptr->m_ssl_stream.native_handle(), sni.get().c_str())) {
      error_code = boost::system::error_code(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
    }
    if (!error_code && m_ptr->m_config.m_peer_verification && m_ptr->m_type == type::client) {
      error_code = configure_expected_peer_identity(m_ptr->m_ssl_stream.native_handle(), sni.get());
    }
  }
  else {
#ifdef DEBUG_BUILD
    m_logger->trace("SNI extension not set");
#endif
  }
  if (!error_code) {
    std::vector<std::string> alpn_protos;
    if (m_ptr->m_config.m_alpn_protos) {
      std::stringstream ss(m_ptr->m_config.m_alpn_protos.get());
      while (ss.good()) {
        std::string proto;
        std::getline(ss, proto, ',');
        if (!proto.empty()) {
          alpn_protos.push_back(proto);
        }
      }
    }
    if (!alpn_protos.empty()) {
#ifdef DEBUG_BUILD
      m_logger->trace("setting ALPN protocols: {}", fmt::join(alpn_protos, ", "));
#endif
      std::vector<unsigned char> alpn_data;
      for (const auto& proto : alpn_protos) {
        alpn_data.push_back(static_cast<unsigned char>(proto.size()));
        alpn_data.insert(alpn_data.end(), proto.begin(), proto.end());
      }
      if (!SSL_set_alpn_protos(m_ptr->m_ssl_stream.native_handle(), alpn_data.data(), static_cast<unsigned int>(alpn_data.size()))) {
        error_code = boost::system::error_code(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
      }
    }
    else {
#ifdef DEBUG_BUILD
      m_logger->trace("ALPN protocols not set");
#endif
    }
  }
  if (error_code) {
    on_complete(error_code);
  }
  else {
    m_ptr->m_next_layer->async_connect(m_endpoint, std::bind(&async_connect_operation::on_connect, shared_from_this(), std::placeholders::_1));
  }
}

void stream_socket_ssl::impl::async_connect_operation::on_connect(const boost::system::error_code& error_code)
{
  if (error_code) {
    on_complete(error_code);
  }
  else {
    do_handshake();
  }
}

void stream_socket_ssl::impl::async_connect_operation::do_handshake()
{
#ifdef DEBUG_BUILD
  m_logger->trace("handshaking with SSL server...");
#endif
  m_ptr->async_handshake(std::bind(&async_connect_operation::on_handshake, shared_from_this(), std::placeholders::_1));
}

void stream_socket_ssl::impl::async_connect_operation::on_handshake(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  m_logger->trace("handshake with SSL server completed [error_code: {}]", (error_code ? error_code.message() : "none"));
#endif
  if (error_code) {
    {
      boost::system::error_code tmp;
      m_ptr->m_next_layer->close(tmp);
    }
    on_complete(error_code);
  }
  else {
    {
      const unsigned char* alpn_selected;
      unsigned int alpn_len;
      ::SSL_get0_alpn_selected(m_ptr->m_ssl_stream.native_handle(), &alpn_selected, &alpn_len);
      if (alpn_len > 0) {
        std::string selected_protocol(reinterpret_cast<const char*>(alpn_selected), alpn_len);
#ifdef DEBUG_BUILD
        m_logger->trace("ALPN protocol selected: {}", selected_protocol);
#endif
      }
      else {
#ifdef DEBUG_BUILD
        m_logger->trace("No ALPN protocol selected");
#endif
      }
    }
    on_complete(error_code);
  }
}

void stream_socket_ssl::impl::async_connect_operation::on_complete(const boost::system::error_code& error_code)
{
  rstream::core::invoke_completion_handler(m_ptr->m_next_layer->get_executor(), std::move(m_handler), error_code);
  m_handler = nullptr;
  m_ptr     = nullptr;
}

stream_socket_ssl::impl::async_shutdown_operation::async_shutdown_operation(ptr ptr, async_shutdown_completion_handler&& handler)
    : m_ptr(ptr),
      m_strand(ptr->m_next_layer->get_executor()),
      m_logger({"rstream", "io", "stream", "ssl", "shtdwn", fmt::format("#{}", fmt::ptr(this))}),
      m_complete(false),
      m_timer(ptr->m_next_layer->get_executor()),
      m_handler(std::move(handler))
{
}

void stream_socket_ssl::impl::async_shutdown_operation::run()
{
  arm_timer(m_ptr->m_config.m_async_shutdown_timeout_ms.value_or(g_async_shutdown_timeout_ms));
  do_shutdown();
}

void stream_socket_ssl::impl::async_shutdown_operation::arm_timer(unsigned long timeout_ms)
{
  if (timeout_ms == 0) {
    return;
  }
  m_timer.expires_from_now(boost::posix_time::milliseconds(timeout_ms));
  auto completion_handler = std::bind(&async_shutdown_operation::on_timer_cb, shared_from_this(), std::placeholders::_1);
  m_timer.async_wait(boost::asio::bind_executor(m_strand, completion_handler));
}

void stream_socket_ssl::impl::async_shutdown_operation::do_shutdown()
{
#if SSL_STREAM_USE_STRAND == 1
#ifdef DEBUG_BUILD
  assert(m_ptr->m_strand.running_in_this_thread());
#endif
#endif
#ifdef DEBUG_BUILD
  m_logger->trace("shutting down SSL stream...");
#endif
  auto completion_handler = std::bind(&async_shutdown_operation::on_shutdown, shared_from_this(), std::placeholders::_1);
  m_ptr->m_ssl_stream.async_shutdown(boost::asio::bind_executor(m_strand, completion_handler));
}

void stream_socket_ssl::impl::async_shutdown_operation::on_timer_cb(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_complete || error_code) {
    return;
  }
  m_complete = true;
  {
    boost::system::error_code tmp;
    m_ptr->m_next_layer->close(tmp);
  }
  on_complete(error::code::operation_aborted);
}

void stream_socket_ssl::impl::async_shutdown_operation::on_shutdown(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
  if (m_complete) {
    return;
  }
  m_complete = true;
  {
    boost::system::error_code tmp;
    m_timer.cancel(tmp);
  }
  on_complete(error_code);
}

void stream_socket_ssl::impl::async_shutdown_operation::on_complete(const boost::system::error_code& error_code)
{
#ifdef DEBUG_BUILD
  assert(m_strand.running_in_this_thread());
#endif
#ifdef DEBUG_BUILD
  m_logger->trace("SSL stream shutdown completed [error_code: {}]", (error_code ? error_code.message() : "none"));
#endif
  rstream::core::invoke_completion_handler(m_ptr->m_next_layer->get_executor(), std::move(m_handler), error_code);
  m_handler = nullptr;
  m_ptr     = nullptr;
}

boost::system::error_code translate_error(long error)
{
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
  if (ERR_SYSTEM_ERROR(error)) {
    return boost::system::error_code(static_cast<int>(ERR_GET_REASON(error)), boost::asio::error::get_system_category());
  }
#endif
  return boost::system::error_code(static_cast<int>(error), boost::asio::error::get_ssl_category());
}

#if SSL_STREAM_USE_OPENSSL_ENGINE == 1
int ssl_ui_reader(UI* ui, UI_STRING* uis)
{
  const char* password;
  switch (UI_get_string_type(uis)) {
    case UIT_PROMPT:
    case UIT_VERIFY:
      password = (const char*)UI_get0_user_data(ui);
      if (password && (UI_get_input_flags(uis) & UI_INPUT_FLAG_DEFAULT_PWD)) {
        UI_set_result(ui, uis, password);
        return 1;
      }
      FALLTHROUGH();
    default:
      break;
  }
  return (UI_method_get_reader(UI_OpenSSL()))(ui, uis);
}

int ssl_ui_writer(UI* ui, UI_STRING* uis)
{
  switch (UI_get_string_type(uis)) {
    case UIT_PROMPT:
    case UIT_VERIFY:
      if (UI_get0_user_data(ui) && (UI_get_input_flags(uis) & UI_INPUT_FLAG_DEFAULT_PWD)) {
        return 1;
      }
      FALLTHROUGH();
    default:
      break;
  }
  return (UI_method_get_writer(UI_OpenSSL()))(ui, uis);
}
#endif

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
