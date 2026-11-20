// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>

#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

#include "tunnel-server.hpp"

#include "http-handler.hpp"

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

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
    sessions.start(std::move(socket), example_http::serve<rstrm::socket>);
  }
}

int main()
{
  return example_tunnel::run(listener);
}
