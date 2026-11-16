// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio.hpp>

#include <rstream/core/exception.hpp>
#include <rstream/io-rstrm/client.hpp>

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

asio::awaitable<void> run()
{
  auto executor = co_await asio::this_coro::executor;
  rstrm::client client(executor);
  try {
    co_await client.async_connect(asio::use_awaitable);
    rstrm::tunnel_properties properties = {.m_publish = false, .m_protocol = rstrm::protocol::tls};
    auto tunnel                         = co_await client.async_create_tunnel(properties, asio::use_awaitable);
    std::cout << "Private tunnel: " << tunnel.local_endpoint() << std::endl;
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
