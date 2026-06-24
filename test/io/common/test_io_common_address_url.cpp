// See LICENSE file in the project root for license information.

#include <cassert>
#include <string>

#include <boost/asio/io_context.hpp>
#include <boost/url.hpp>

#include <rstream/io/address.hpp>
#include <rstream/io/detail/stream/acceptor.hpp>
#include <rstream/io/detail/stream/endpoint.hpp>
#include <rstream/io/detail/stream/error.hpp>
#include <rstream/io/detail/stream/factory.hpp>
#include <rstream/io/detail/stream/ssl.hpp>
#include <rstream/io/detail/stream/stream_socket.hpp>
#include <rstream/io/detail/stream/url.hpp>
#include <rstream/io/error.hpp>

static boost::urls::url parse_url(const std::string& uri)
{
  auto parsed = boost::urls::parse_uri(uri);
  assert(parsed);
  return boost::urls::url(parsed.value());
}

static void assert_stream_error(const boost::system::error_code& actual, rstream::io::detail::stream::error::code expected)
{
  assert(actual.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(actual.value() == static_cast<int>(expected));
}

static void check_address_normalization()
{
  auto port_only = rstream::io::make_address("8080");
  assert(port_only.host() == "localhost");
  assert(port_only.port() == "8080");

  auto host_port = rstream::io::make_address("edge.example:443");
  assert(host_port.host() == "edge.example");
  assert(host_port.port() == "443");

  auto explicit_scheme = rstream::io::make_address("https://service.example/path");
  assert(explicit_scheme.host() == "service.example");
  assert(explicit_scheme.port().empty());
}

static void check_url_boolean_parsing()
{
  auto url = parse_url("tcp://edge.example?ssl&retry=false&strict=TRUE");
  bool ssl = false;
  boost::system::error_code error_code;
  rstream::io::detail::stream::parse_url_param_value(ssl, *url.params().find("ssl"), error_code);
  assert(!error_code);
  assert(ssl);

  bool retry = true;
  rstream::io::detail::stream::parse_url_param_value(retry, *url.params().find("retry"), error_code);
  assert(!error_code);
  assert(!retry);

  bool strict = false;
  rstream::io::detail::stream::parse_url_param_value(strict, *url.params().find("strict"), error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::invalid_argument);
}

static void check_url_string_and_integer_parsing()
{
  auto url = parse_url("tcp://edge.example?name=api&missing&size=42&bad_size=nope");
  boost::system::error_code error_code;
  boost::optional<std::string> name;
  rstream::io::detail::stream::parse_url_param_value(name, *url.params().find("name"), error_code);
  assert(!error_code);
  assert(name);
  assert(name.value() == "api");

  boost::optional<std::string> missing;
  rstream::io::detail::stream::parse_url_param_value(missing, *url.params().find("missing"), error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::invalid_argument);

  error_code = {};
  boost::optional<unsigned long> size;
  rstream::io::detail::stream::parse_url_param_value(size, *url.params().find("size"), error_code);
  assert(!error_code);
  assert(size);
  assert(size.value() == 42);

  boost::optional<unsigned long> bad_size;
  rstream::io::detail::stream::parse_url_param_value(bad_size, *url.params().find("bad_size"), error_code);
  assert_stream_error(error_code, rstream::io::detail::stream::error::code::invalid_argument);
}

static void check_ssl_url_config_parsing()
{
  namespace stream = rstream::io::detail::stream;
  boost::system::error_code error_code;
  auto disabled = stream::parse_ssl_config(parse_url("tcp://edge.example:443"), error_code);
  assert(!error_code);
  assert(!disabled);

  auto enabled = stream::parse_ssl_config(parse_url(
                                              "tcp://edge.example:443?ssl&ssl.tlsv13&ssl.peer_verification=false&ssl.request_peer_cert=false"
                                              "&ssl.client_rpk=true&ssl.server_rpk=true&ssl.key=key&ssl.key_file=key.pem&ssl.key_type=PEM"
                                              "&ssl.passphrase=secret&ssl.cert=cert&ssl.cert_file=cert.pem&ssl.cert_type=PEM"
                                              "&ssl.tmp_dh_params=dh&ssl.tmp_dh_params_file=dh.pem&ssl.cacert=ca"
                                              "&ssl.cacert_file=ca.pem&ssl.capath=/etc/ssl&ssl.ciphers=DEFAULT"
                                              "&ssl.ciphers_tlsv13=TLS_AES_128_GCM_SHA256&ssl.groups=SecP256r1MLKEM768%3ASecP384r1MLKEM1024&ssl.sni=edge.example"
                                              "&ssl.alpn_protos=rstrm%2F1&ssl.engine=pkcs11"
                                              "&ssl.max_ongoing_upstream_ops=7&ssl.async_shutdown_timeout_ms=250"),
                                          error_code);
  assert(!error_code);
  assert(enabled);
  assert(!enabled->m_tlsv12);
  assert(enabled->m_tlsv13);
  assert(!enabled->m_peer_verification);
  assert(!enabled->m_request_peer_cert);
  assert(enabled->m_client_rpk);
  assert(enabled->m_server_rpk);
  assert(enabled->m_key.value() == "key");
  assert(enabled->m_key_file.value() == "key.pem");
  assert(enabled->m_key_type.value() == "PEM");
  assert(enabled->m_passphrase.value() == "secret");
  assert(enabled->m_cert.value() == "cert");
  assert(enabled->m_cert_file.value() == "cert.pem");
  assert(enabled->m_cert_type.value() == "PEM");
  assert(enabled->m_tmp_dh_params.value() == "dh");
  assert(enabled->m_tmp_dh_params_file.value() == "dh.pem");
  assert(enabled->m_cacert.value() == "ca");
  assert(enabled->m_cacert_file.value() == "ca.pem");
  assert(enabled->m_capath.value() == "/etc/ssl");
  assert(enabled->m_ciphers.value() == "DEFAULT");
  assert(enabled->m_ciphers_tlsv13.value() == "TLS_AES_128_GCM_SHA256");
  assert(enabled->m_groups.value() == "SecP256r1MLKEM768:SecP384r1MLKEM1024");
  assert(enabled->m_sni.value() == "edge.example");
  assert(enabled->m_alpn_protos.value() == "rstrm/1");
  assert(enabled->m_engine.value() == "pkcs11");
  assert(enabled->m_max_ongoing_upstream_ops.value() == 7);
  assert(enabled->m_async_shutdown_timeout_ms.value() == 250);

  error_code   = {};
  auto invalid = stream::parse_ssl_config(parse_url("tcp://edge.example:443?ssl&ssl.peer_verification=maybe"), error_code);
  assert(!invalid);
  assert_stream_error(error_code, stream::error::code::invalid_argument);
}

static void check_stream_factory_rejects_unavailable_protocols()
{
  namespace stream = rstream::io::detail::stream;

  boost::asio::io_context io_context;
  stream::factory factory;
  boost::system::error_code error_code;

  stream::endpoint_base::protocol_type unknown = std::string("unknown");
  auto missing                                 = factory.socket(io_context.get_executor(), unknown, error_code);
  assert(!missing);
  assert(error_code);

  error_code                                   = {};
  stream::endpoint_base::protocol_type invalid = boost::system::errc::make_error_code(boost::system::errc::invalid_argument);
  auto invalid_socket                          = factory.socket(io_context.get_executor(), invalid, error_code);
  assert(!invalid_socket);
  assert_stream_error(error_code, stream::error::code::uninitialized_object);
}

static void check_io_error_messages()
{
  auto code = rstream::io::error::make_error_code(rstream::io::error::code::invalid_buffer_size);
  assert(code.category() == rstream::io::error::rstream_io_error_category());
  assert(code.message() == "invalid buffer size");
  assert(rstream::io::to_string(rstream::io::error::code::success) == "success");
  assert(rstream::io::to_string(rstream::io::error::code::deserialization_error) == "deserialization error");
  assert(rstream::io::to_string(rstream::io::error::code::operation_cancelled) == "operation has been cancelled");
  assert(rstream::io::to_string(rstream::io::error::code::operation_timeout) == "operation timeout");
  assert(rstream::io::to_string(rstream::io::error::code::unknown_undefined_error) == "error is unknonw / undefined");
  assert(rstream::io::to_string(rstream::io::error::code::invalid_uri) == "invalid URI");
  assert(rstream::io::to_string(rstream::io::error::code::unsupported_operation) == "unsupported operation");
  assert(rstream::io::to_string(static_cast<rstream::io::error::code>(999)) == "unknown error");
}

static void check_stream_error_messages()
{
  auto code = rstream::io::detail::stream::error::make_error_code(rstream::io::detail::stream::error::code::ssl_configuration_error);
  assert(code.category() == rstream::io::detail::stream::error::rstream_io_detail_stream_error_category());
  assert(code.message() == "SSL configuration error");
  assert(rstream::io::detail::stream::to_string(rstream::io::detail::stream::error::code::success) == "success");
  assert(rstream::io::detail::stream::to_string(rstream::io::detail::stream::error::code::generic_error) == "generic error");
  assert(rstream::io::detail::stream::to_string(rstream::io::detail::stream::error::code::operation_aborted) == "operation aborted");
  assert(rstream::io::detail::stream::to_string(rstream::io::detail::stream::error::code::operation_in_progress) == "another operation is in progress");
  assert(rstream::io::detail::stream::to_string(rstream::io::detail::stream::error::code::uninitialized_object) == "uninitialized object");
  assert(rstream::io::detail::stream::to_string(static_cast<rstream::io::detail::stream::error::code>(999)) == "unknown error");
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_address_normalization();
  check_url_boolean_parsing();
  check_url_string_and_integer_parsing();
  check_ssl_url_config_parsing();
  check_stream_factory_rejects_unavailable_protocols();
  check_io_error_messages();
  check_stream_error_messages();
  return 0;
}
