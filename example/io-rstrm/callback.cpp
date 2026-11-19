// See LICENSE file in the project root for license information.

#include <iostream>
#include <memory>

#include <boost/asio.hpp>

#include <rstream/io-rstrm/client.hpp>

namespace asio  = boost::asio;
namespace rstrm = rstream::io_rstrm;

class application : public std::enable_shared_from_this<application> {
 public:
  application(asio::io_context& io, int& result)
      : m_client(io.get_executor()),
        m_result(result)
  {
  }

  void start()
  {
    m_client.async_connect([self = shared_from_this()](const boost::system::error_code& ec) {
      if (ec) {
        self->finish(ec);
        return;
      }
      rstrm::tunnel_properties properties{};
      properties.m_publish  = false;
      properties.m_protocol = rstrm::protocol::tls;
      self->m_client.async_create_tunnel(properties, [self](const boost::system::error_code& error, rstrm::tunnel tunnel) {
        if (!error) {
          std::cout << "Private tunnel: " << tunnel.local_endpoint() << std::endl;
          tunnel.close();
        }
        self->finish(error);
      });
    });
  }

 private:
  void finish(const boost::system::error_code& ec)
  {
    if (ec) {
      m_result = 1;
      std::cerr << ec.message() << std::endl;
    }
    m_client.close();
  }

  rstrm::client m_client;
  int& m_result;
};

int main()
{
  asio::io_context io;
  int result = 0;
  auto app   = std::make_shared<application>(io, result);
  app->start();
  io.run();
  return result;
}
