// See LICENSE file in the project root for license information.

#include <cassert>
#include <cstdlib>
#include <iostream>

#include <boost/asio/io_context.hpp>
#include <boost/url.hpp>

#include <rstream/io-rstrm/acceptor.hpp>
#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io-rstrm/error.hpp>
#include <rstream/io-rstrm/resolver.hpp>
#include <rstream/io-rstrm/socket.hpp>

class env_guard {
 public:
  explicit env_guard(const char* key)
      : m_key(key)
  {
    const char* value = std::getenv(key);
    if (value) {
      m_present = true;
      m_value   = value;
    }
  }

  ~env_guard()
  {
    if (m_present) {
      setenv(m_key, m_value.c_str(), 1);
    }
    else {
      unsetenv(m_key);
    }
  }

  void unset()
  {
    unsetenv(m_key);
  }

  void set(const std::string& value)
  {
    setenv(m_key, value.c_str(), 1);
  }

 private:
  const char* m_key;
  bool m_present = false;
  std::string m_value;
};

static boost::urls::url parse_url(const std::string& uri)
{
  auto parsed = boost::urls::parse_uri(uri);
  assert(parsed);
  return boost::urls::url(parsed.value());
}

static void assert_error_code(const boost::system::error_code& actual, int value, const char* category)
{
  if (actual.value() != value || std::string(actual.category().name()) != category) {
    std::cerr << "unexpected error code: value=" << actual.value()
              << " category=" << actual.category().name()
              << " message=" << actual.message() << std::endl;
    assert(false);
  }
}

static void check_make_endpoint_from_explicit_inputs()
{
  auto endpoint = rstream::io_rstrm::make_endpoint(std::string("api"), std::string("tcp://engine.example:443?ssl"));
  assert(endpoint);
  assert(endpoint.value().m_id_name);
  assert(endpoint.value().m_id_name.value() == "api");
  assert(endpoint.value().m_server_address.host() == "engine.example");
  assert(endpoint.value().m_server_address.port() == "443");
  assert(!endpoint.value().m_server_address_from_uri_param);
}

static void check_make_endpoint_from_uri_server_param()
{
  env_guard engine_address("RSTREAM_ENGINE_ADDRESS");
  env_guard engine("RSTREAM_ENGINE");
  env_guard config_path("RSTREAM_CONFIG");
  engine_address.unset();
  engine.unset();
  config_path.unset();
  auto endpoint = rstream::io_rstrm::make_endpoint(parse_url("rstrm://viewer?server=tcp%3A%2F%2Fedge.example%3A443%3Fssl"));
  assert(endpoint);
  assert(endpoint.value().m_id_name);
  assert(endpoint.value().m_id_name.value() == "viewer");
  assert(endpoint.value().m_server_address.host() == "edge.example");
  assert(endpoint.value().m_server_address.port() == "443");
  assert(endpoint.value().m_server_address_from_uri_param);
}

static void check_make_redirected_server_address_uses_target_sni()
{
  const auto base = rstream::io::make_address("tcp://owner.example:443?ssl&ssl.sni=project.example&ssl.alpn_protos=rstrm%2F1");
  auto redirected = rstream::io_rstrm::make_redirected_server_address(base, "ingress.example:8443");
  assert(redirected);
  assert(redirected->host() == "ingress.example");
  assert(redirected->port() == "8443");
  const auto params = redirected->m_url.params();
  const auto alpn   = params.find("ssl.alpn_protos");
  assert(params.contains("ssl"));
  assert(!params.contains("ssl.sni"));
  assert(alpn != params.end() && (*alpn).value == "rstrm/1");
  assert(!rstream::io_rstrm::make_redirected_server_address(base, "missing-port.example"));
}

static void check_make_endpoint_fails_closed_without_engine()
{
  env_guard home("HOME");
  env_guard userprofile("USERPROFILE");
  env_guard engine_address("RSTREAM_ENGINE_ADDRESS");
  env_guard engine("RSTREAM_ENGINE");
  env_guard config_path("RSTREAM_CONFIG");
  home.unset();
  userprofile.unset();
  engine_address.unset();
  engine.unset();
  config_path.unset();
  auto endpoint = rstream::io_rstrm::make_endpoint(boost::urls::url("rstrm://viewer"));
  assert(!endpoint);
  assert_error_code(endpoint.error(), static_cast<int>(boost::system::errc::no_such_file_or_directory), boost::system::generic_category().name());
}

static void check_resolver_delivers_single_endpoint()
{
  boost::asio::io_context io_context;
  rstream::io_rstrm::resolver resolver(io_context.get_executor());
  bool called = false;
  resolver.async_resolve(parse_url("rstrm://api?server=tcp%3A%2F%2Fresolver.example%3A443%3Fssl"),
                         [&](const boost::system::error_code& error_code, const rstream::io_rstrm::resolver::results_type& results) {
                           called = true;
                           assert(!error_code);
                           assert(results.size() == 1);
                           auto endpoint = results.begin()->endpoint();
                           assert(endpoint.m_id_name);
                           assert(endpoint.m_id_name.value() == "api");
                           assert(endpoint.m_server_address.host() == "resolver.example");
                           assert(endpoint.m_server_address.port() == "443");
                         });
  io_context.run();
  assert(called);
}

static void check_socket_rejects_ambient_token_with_uri_server_param()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  token.set("ambient-token");
  boost::asio::io_context io_context;
  auto endpoint = rstream::io_rstrm::make_endpoint(parse_url("rstrm://viewer?server=tcp%3A%2F%2F127.0.0.1%3A1"));
  assert(endpoint);
  assert(endpoint.value().m_server_address_from_uri_param);

  rstream::io_rstrm::socket socket(io_context.get_executor());
  bool called = false;
  socket.async_connect(endpoint.value(), [&](const boost::system::error_code& error_code) {
    called = true;
    assert_error_code(error_code, static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  io_context.run();
  assert(called);
}

static void check_acceptor_rejects_ambient_token_with_uri_server_param()
{
  env_guard token("RSTREAM_AUTHENTICATION_TOKEN");
  token.set("ambient-token");
  boost::asio::io_context io_context;
  auto endpoint = rstream::io_rstrm::make_endpoint(parse_url("rstrm://viewer?server=tcp%3A%2F%2F127.0.0.1%3A1"));
  assert(endpoint);
  assert(endpoint.value().m_server_address_from_uri_param);

  rstream::io_rstrm::acceptor acceptor(io_context.get_executor());
  boost::system::error_code error_code;
  acceptor.bind(endpoint.value(), error_code);
  assert(!error_code);
  rstream::io_rstrm::socket peer(io_context.get_executor());
  rstream::io_rstrm::endpoint peer_endpoint;
  bool called = false;
  acceptor.async_accept(peer, peer_endpoint, [&](const boost::system::error_code& error) {
    called = true;
    assert_error_code(error, static_cast<int>(rstream::io_rstrm::error::code::invalid_configuration), rstream::io_rstrm::error::rstream_rstream_error_category().name());
  });
  io_context.run();
  assert(called);
}

static void check_resolver_rejects_invalid_uri()
{
  boost::asio::io_context io_context;
  rstream::io_rstrm::resolver resolver(io_context.get_executor());
  bool called = false;
  resolver.async_resolve("://bad", [&](const boost::system::error_code& error_code, const rstream::io_rstrm::resolver::results_type& results) {
    called = true;
    assert(error_code);
    assert(results.empty());
  });
  io_context.run();
  assert(called);
}

int main(int argc, char** argv)
{
  (void)argc;
  (void)argv;
  check_make_endpoint_from_explicit_inputs();
  check_make_endpoint_from_uri_server_param();
  check_make_redirected_server_address_uses_target_sni();
  check_make_endpoint_fails_closed_without_engine();
  check_resolver_delivers_single_endpoint();
  check_resolver_rejects_invalid_uri();
  check_socket_rejects_ambient_token_with_uri_server_param();
  check_acceptor_rejects_ambient_token_with_uri_server_param();
  return 0;
}
