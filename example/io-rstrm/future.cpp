// See LICENSE file in the project root for license information.

#include <future>
#include <iostream>
#include <thread>

#include <boost/asio.hpp>

#include <rstream/io-rstrm/client.hpp>

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

int main()
{
  asio::io_context io;
  auto work = asio::make_work_guard(io);
  rstrm::client client(io.get_executor());
  // The application creates this thread. get() below never blocks the loop.
  std::thread worker([&] { io.run(); });
  int result = 0;
  try {
    client.async_connect(asio::use_future).get();
    rstrm::tunnel_properties properties{};
    properties.m_publish  = false;
    properties.m_protocol = rstrm::protocol::tls;
    auto tunnel           = client.async_create_tunnel(properties, asio::use_future).get();
    std::cout << "Private tunnel: " << tunnel.local_endpoint() << std::endl;
    tunnel.close();
  }
  catch (const std::exception& error) {
    result = 1;
    std::cerr << error.what() << std::endl;
  }
  client.close();
  work.reset();
  worker.join();
  return result;
}
