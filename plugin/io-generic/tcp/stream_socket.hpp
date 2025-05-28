// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/ip/tcp.hpp>

#include <rstream/io/detail/stream/stream_socket_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

using stream_socket = rstream::io::detail::stream::stream_socket_impl<boost::asio::ip::tcp::socket>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::tcp::stream_socket::configure_internal(rstream::io::detail::stream::socket_mode mode, bool connected, const boost::asio::ip::tcp::endpoint& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);
