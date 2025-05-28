// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/ip/tcp.hpp>

#include <rstream/io/detail/stream/endpoint_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

using endpoint = rstream::io::detail::stream::endpoint_impl<boost::asio::ip::tcp::endpoint>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
std::string rstream::plugin::io_generic::tcp::endpoint::to_string() const;
