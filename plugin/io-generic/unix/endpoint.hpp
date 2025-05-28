// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/io/detail/stream/endpoint_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

using endpoint = rstream::io::detail::stream::endpoint_impl<boost::asio::local::stream_protocol::endpoint>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
std::string rstream::plugin::io_generic::unix_::endpoint::to_string() const;
