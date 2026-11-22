// See LICENSE file in the project root for license information.

#include <iostream>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

#include "tunnel-server.hpp"

namespace asio  = boost::asio;
namespace http  = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> session(rstrm::socket stream, asio::ssl::context& context)
{
  asio::ssl::stream<rstrm::socket> socket(std::move(stream), context);
  try {
    co_await socket.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
    http::request<http::string_body> req;
    boost::beast::flat_buffer buffer;
    co_await http::async_read(socket, buffer, req, asio::use_awaitable);
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(false);
    res.body() = "Hello from rstream-cpp";
    res.prepare_payload();
    co_await http::async_write(socket, res, asio::use_awaitable);
    boost::system::error_code ec;
    co_await socket.async_shutdown(asio::redirect_error(asio::use_awaitable, ec));
  }
  catch (const std::exception& e) {
    std::cerr << "session error: " << e.what() << std::endl;
  }
  boost::system::error_code ignored;
  socket.next_layer().close(ignored);
}

asio::awaitable<void> listener(rstrm::client& client, example_tunnel::sessions& sessions, asio::ssl::context& context, const std::string& hostname)
{
  auto executor = co_await asio::this_coro::executor;
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties{};
  properties.m_publish                = true;
  properties.m_protocol               = rstrm::protocol::tls;
  properties.m_tls_mode               = rstrm::tls_mode::passthrough;
  if (!hostname.empty()) {
    properties.m_hostname = hostname;
  }
  auto tunnel = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  auto forwarding = rstrm::format_forwarding_address(tunnel.properties());
  if (forwarding) {
    std::cout << "Forwarding address: " << forwarding.value() << std::endl;
  }
  while (!sessions.stopping()) {
    rstrm::socket socket(executor);
    rstrm::endpoint peer;
    co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
    sessions.start(std::move(socket), [&context](rstrm::socket stream) { return session(std::move(stream), context); });
  }
}

int run(int argc, char** argv)
{
  if (argc != 3 && argc != 4) {
    std::cerr << "usage: rstream-example-tls-http-server CERTIFICATE_CHAIN_PEM PRIVATE_KEY_PEM [FORWARDING_HOSTNAME]" << std::endl;
    return 1;
  }
  asio::ssl::context context(asio::ssl::context::tls_server);
  context.use_certificate_chain_file(argv[1]);
  context.use_private_key_file(argv[2], asio::ssl::context::pem);
  if (!SSL_CTX_set_min_proto_version(context.native_handle(), TLS1_2_VERSION)) {
    std::cerr << "cannot set the application TLS minimum version" << std::endl;
    return 1;
  }
  const std::string hostname = argc == 4 ? argv[3] : "";
  return example_tunnel::run([&](rstrm::client& client, example_tunnel::sessions& sessions) {
    return listener(client, sessions, context, hostname);
  });
}

int main(int argc, char** argv)
{
  try {
    return run(argc, argv);
  }
  catch (const std::exception& error) {
    std::cerr << "TLS setup error: " << error.what() << std::endl;
    return 1;
  }
}
