// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/io/detail/stream/acceptor_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

using acceptor = rstream::io::detail::stream::acceptor_impl<boost::asio::local::stream_protocol::acceptor, boost::asio::local::stream_protocol::socket>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::unix_::acceptor::configure_internal(const boost::asio::local::stream_protocol::endpoint& endpoint, const boost::urls::url& url, boost::system::error_code& error_code);
