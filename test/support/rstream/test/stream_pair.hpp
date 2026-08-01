// See LICENSE file in the project root for license information.

#pragma once

#ifdef _WIN32
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#else
#include <boost/asio/local/connect_pair.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#endif

namespace rstream {
namespace test {

#ifdef _WIN32
using stream_socket = boost::asio::ip::tcp::socket;
#else
using stream_socket = boost::asio::local::stream_protocol::socket;
#endif

inline void connect_stream_pair(stream_socket& first, stream_socket& second)
{
#ifdef _WIN32
  using boost::asio::ip::tcp;
  tcp::acceptor acceptor(first.get_executor(), tcp::endpoint(tcp::v4(), 0));
  first.connect(tcp::endpoint(boost::asio::ip::address_v4::loopback(), acceptor.local_endpoint().port()));
  acceptor.accept(second);
#else
  boost::asio::local::connect_pair(first, second);
#endif
}

}  // namespace test
}  // namespace rstream
