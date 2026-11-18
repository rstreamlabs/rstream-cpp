// See LICENSE file in the project root for license information.

#include <csignal>
#include <iostream>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/beast.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/io-rstrm.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio  = boost::asio;
namespace http  = boost::beast::http;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> session(rstrm::socket socket)
{
  boost::beast::websocket::stream<rstrm::socket> ws(std::move(socket));
  ws.read_message_max(64 * 1024);
  ws.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::server));
  try {
    co_await ws.async_accept(asio::use_awaitable);
    for (;;) {
      boost::beast::flat_buffer buffer;
      co_await ws.async_read(buffer, asio::use_awaitable);
      ws.text(ws.got_text());
      co_await ws.async_write(buffer.data(), asio::use_awaitable);
    }
  }
  catch (const boost::system::system_error& error) {
    if (error.code() != boost::beast::websocket::error::closed) {
      std::cerr << "WebSocket error: " << error.what() << std::endl;
    }
  }
  // The SDK's Beast teardown completes without another transport handshake.
  // Close explicitly after all WebSocket operations have completed.
  boost::system::error_code ignored;
  ws.next_layer().close(ignored);
}

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
    asio::co_spawn(executor, session(std::move(socket)), asio::detached);
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
