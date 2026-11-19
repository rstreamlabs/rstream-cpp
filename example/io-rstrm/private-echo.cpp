// See LICENSE file in the project root for license information.

#include <array>
#include <chrono>
#include <iostream>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>
#include <rstream/io-rstrm/socket.hpp>

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> serve_echo(rstrm::tunnel& tunnel)
{
  auto executor = co_await asio::this_coro::executor;
  rstrm::socket peer(executor);
  rstrm::endpoint remote;
  std::array<char, 5> data{};
  co_await tunnel.async_accept(peer, remote, asio::use_awaitable);
  co_await asio::async_read(peer, asio::buffer(data), asio::use_awaitable);
  co_await asio::async_write(peer, asio::buffer(data), asio::use_awaitable);
  peer.close();
}

asio::awaitable<void> dial_echo(rstrm::endpoint endpoint)
{
  auto executor = co_await asio::this_coro::executor;
  rstrm::socket socket(executor);
  std::array<char, 5> reply{};
  const std::string message = "hello";
  co_await socket.async_connect(endpoint, asio::use_awaitable);
  co_await asio::async_write(socket, asio::buffer(message), asio::use_awaitable);
  co_await asio::async_read(socket, asio::buffer(reply), asio::use_awaitable);
  if (std::string(reply.data(), reply.size()) != message) {
    throw std::runtime_error("echo did not match");
  }
  std::cout << "Echo: " << message << std::endl;
  socket.close();
}

asio::awaitable<void> run()
{
  using namespace asio::experimental::awaitable_operators;
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  try {
    co_await client.async_connect(asio::use_awaitable);
    rstrm::tunnel_properties properties{};
    properties.m_name     = std::string("echo");
    properties.m_publish  = false;
    properties.m_protocol = rstrm::protocol::tls;
    auto tunnel           = co_await client.async_create_tunnel(properties, asio::use_awaitable);
    boost::system::error_code ec;
    auto address = client.address(ec);
    if (ec) {
      throw boost::system::system_error(ec);
    }
    rstrm::endpoint endpoint{};
    endpoint.m_id_name        = std::string("echo");
    endpoint.m_server_address = address;
    // Both operations finish before their referenced tunnel or buffers die.
    // Failure cancels the other operation instead of losing a detached error.
    asio::steady_timer deadline(executor, std::chrono::seconds(10));
    auto [order, exchange_error, deadline_error] = co_await asio::experimental::make_parallel_group(
                                                       asio::co_spawn(executor, serve_echo(tunnel) && dial_echo(endpoint), asio::deferred),
                                                       deadline.async_wait(asio::deferred))
                                                       .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);
    if (order[0] == 1 && !deadline_error) {
      throw boost::system::system_error(asio::error::timed_out);
    }
    if (exchange_error) {
      std::rethrow_exception(exchange_error);
    }
    tunnel.close();
  }
  catch (...) {
    client.close();
    throw;
  }
  client.close();
}

int main()
{
  asio::io_context io;
  int result = 0;
  asio::co_spawn(io, run(), [&](std::exception_ptr error) {
    if (error) {
      result = 1;
      std::cerr << rstream::core::throwable::to_string(error) << std::endl;
    }
  });
  io.run();
  return result;
}
