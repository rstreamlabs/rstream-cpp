// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/io/detail/stream/stream_socket_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

using stream_socket = rstream::io::detail::stream::stream_socket_impl<boost::asio::local::stream_protocol::socket>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
