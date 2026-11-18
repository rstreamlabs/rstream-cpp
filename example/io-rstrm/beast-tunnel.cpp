// See LICENSE file in the project root for license information.

#include <csignal>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

#include "http-handler.hpp"

namespace asio  = boost::asio;
namespace http  = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> listener()
{
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  co_await client.async_connect(asio::use_awaitable);
  rstrm::tunnel_properties properties{};
  properties.m_publish                = true;
  properties.m_protocol               = rstrm::protocol::http;
  auto tunnel                         = co_await client.async_create_tunnel(properties, asio::use_awaitable);
  auto forwarding                     = rstrm::format_forwarding_address(tunnel.properties());
  if (forwarding) {
    std::cout << "Forwarding address: " << forwarding.value() << std::endl;
  }
  while (true) {
    rstrm::socket socket(executor);
    rstrm::endpoint peer;
    co_await tunnel.async_accept(socket, peer, asio::use_awaitable);
    asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
  }
}

int main()
{
  int result = 0;
  asio::io_context io_context;
  asio::signal_set signals(io_context, SIGINT, SIGTERM);
  auto on_done = [&io_context, &result](std::exception_ptr error) {
    if (error) {
      result = 1;
      std::cerr << "fatal error: " << rstream::core::throwable::to_string(error) << std::endl;
    }
    io_context.stop();
  };
  signals.async_wait([&on_done](const boost::system::error_code&, int) { on_done(nullptr); });
  asio::co_spawn(io_context, listener(), on_done);
  io_context.run();
  return result;
}
