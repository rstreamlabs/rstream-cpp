// See LICENSE file in the project root for license information.

#include <csignal>
#include <iostream>
#include <string>

#include <boost/asio.hpp>

#include "http-handler.hpp"

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

asio::awaitable<void> listener(unsigned short port)
{
  auto executor = co_await asio::this_coro::executor;
  tcp::acceptor acceptor(executor, tcp::endpoint(asio::ip::address_v4::loopback(), port));
  std::cout << "Local address: " << acceptor.local_endpoint() << std::endl;
  for (;;) {
    auto socket = co_await acceptor.async_accept(asio::use_awaitable);
    asio::co_spawn(executor, example_http::serve(std::move(socket)), asio::detached);
  }
}

int main(int argc, char** argv)
{
  try {
    if (argc != 2) {
      throw std::runtime_error("usage: rstream-example-beast-local PORT (0 selects an ephemeral port)");
    }
    std::size_t consumed = 0;
    auto port            = std::stoul(argv[1], &consumed);
    if (consumed != std::string(argv[1]).size() || port > 65535) {
      throw std::runtime_error("port must be in 0..65535");
    }
    asio::io_context io;
    asio::signal_set signals(io, SIGINT, SIGTERM);
    int result = 0;
    signals.async_wait([&](const boost::system::error_code&, int) { io.stop(); });
    asio::co_spawn(io, listener(static_cast<unsigned short>(port)), [&](std::exception_ptr error) {
      if (error) {
        result = 1;
        try {
          std::rethrow_exception(error);
        }
        catch (const std::exception& failure) {
          std::cerr << failure.what() << std::endl;
        }
      }
      io.stop();
    });
    io.run();
    return result;
  }
  catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }
}
