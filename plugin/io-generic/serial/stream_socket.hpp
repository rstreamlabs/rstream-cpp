// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/serial_port.hpp>

#include <rstream/io/detail/stream/stream_socket_impl.hpp>

#include "endpoint.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

using stream_socket = rstream::io::detail::stream::stream_socket_impl<boost::asio::serial_port, descriptor>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::serial::stream_socket::open_internal(const rstream::plugin::io_generic::serial::descriptor& endpoint, boost::system::error_code& error_code);

template <>
void rstream::plugin::io_generic::serial::stream_socket::configure_internal(rstream::io::detail::stream::socket_mode mode, bool connected, const rstream::plugin::io_generic::serial::descriptor& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);

template <>
rstream::plugin::io_generic::serial::descriptor rstream::plugin::io_generic::serial::stream_socket::remote_endpoint_internal(boost::system::error_code& error_code);

template <>
void rstream::plugin::io_generic::serial::stream_socket::async_connect_internal(const rstream::plugin::io_generic::serial::descriptor& endpoint, async_connect_completion_handler&& handler);
