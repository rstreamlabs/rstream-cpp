// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio/ip/tcp.hpp>

#include <rstream/io/payloader.hpp>

static const unsigned short g_port = 1234;

static const std::size_t g_mtu = 1500;

struct session : public std::enable_shared_from_this<session> {
  session(boost::asio::ip::tcp::socket socket)
      : m_socket(std::move(socket)),
        m_buffer(rstream::core::make_buffer_allocated(g_mtu)),
        m_payloader(m_socket)
  {
  }

  void run()
  {
    do_recv();
  }

  void do_recv()
  {
    m_payloader.async_recv(m_buffer, std::bind(&session::on_recv, shared_from_this(), std::placeholders::_1));
  }

  void on_recv(const boost::system::error_code& error_code)
  {
    if (!error_code) {
      std::cout << "received " << m_buffer.get_size() << " byte(s)" << std::endl;
      do_send();
    }
  }

  void do_send()
  {
    m_payloader.async_send(m_buffer, std::bind(&session::on_send, shared_from_this(), std::placeholders::_1));
  }

  void on_send(const boost::system::error_code& error_code)
  {
    if (!error_code) {
      m_buffer.reset_size();
      do_recv();
    }
  }

  boost::asio::ip::tcp::socket m_socket;

  rstream::core::buffer m_buffer;

  rstream::io::payloader<boost::asio::ip::tcp::socket&> m_payloader;
};

struct server : public std::enable_shared_from_this<server> {
  server(const boost::asio::io_context::executor_type& executor, unsigned short port)
      : m_acceptor(executor, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))

  {
  }

  void run()
  {
    do_accept();
  }

  void do_accept()
  {
    m_acceptor.async_accept(std::bind(&server::on_accept, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
  }

  void on_accept(const boost::system::error_code& error_code, boost::asio::ip::tcp::socket socket)
  {
    if (!error_code) {
      std::make_shared<session>(std::move(socket))->run();
    }
    do_accept();
  }

  boost::asio::ip::tcp::acceptor m_acceptor;
};

int main()
{
  boost::asio::io_context io_context;
  std::make_shared<server>(io_context.get_executor(), g_port)->run();
  io_context.run();
  return 0;
}
