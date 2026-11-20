// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

#include "tunnel-server.hpp"

namespace asio  = boost::asio;
namespace http  = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> session(rstrm::socket socket)
{
  try {
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
    socket.close(ec);
  }
  catch (const std::exception& e) {
    std::cerr << "session error: " << e.what() << std::endl;
  }
}

asio::awaitable<void> listener(rstrm::client& client, example_tunnel::sessions& sessions)
{
  auto executor = co_await asio::this_coro::executor;
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties{};
  properties.m_publish                = true;
  properties.m_protocol               = rstrm::protocol::http;
  auto tunnel                         = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  auto forwarding                     = rstrm::format_forwarding_address(tunnel.properties());
  if (forwarding) {
    std::cout << "Forwarding address: " << forwarding.value() << std::endl;
  }
  while (!sessions.stopping()) {
    rstrm::socket socket(executor);
    rstrm::endpoint peer;
    co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
    sessions.start(std::move(socket), session);
  }
}

int main()
{
  return example_tunnel::run(listener);
}
