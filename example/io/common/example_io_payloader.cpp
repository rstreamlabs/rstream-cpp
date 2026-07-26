// See LICENSE file in the project root for license information.

#include <iostream>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_future.hpp>

#include <rstream/io/payloader.hpp>

int main()
{
  boost::asio::io_context io_context;
  boost::asio::ip::tcp::socket socket(io_context);
  rstream::io::payloader<boost::asio::ip::tcp::socket&> payloader(socket);
  // try 'async_send'
  {
    rstream::core::buffer buffer;
    auto handler = [](boost::system::error_code) {
      // do something
    };
    payloader.async_send(buffer, handler);
    payloader.async_send(buffer, boost::asio::use_future).get();
  }
  // try 'async_recv'
  {
    rstream::core::buffer buffer;
    auto handler = [](boost::system::error_code) {
      // do something
    };
    payloader.async_recv(buffer, handler);
    payloader.async_recv(buffer, boost::asio::use_future).get();
  }
  return 0;
}
