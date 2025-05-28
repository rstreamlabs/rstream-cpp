// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/local/stream_protocol.hpp>

#include <rstream/io/detail/stream/resolver_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

using resolver = rstream::io::detail::stream::basic_resolver_impl<boost::asio::local::stream_protocol::endpoint>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::unix_::resolver::resolve_internal(const boost::urls::url& url, boost::asio::local::stream_protocol::endpoint& endpoint, boost::system::error_code& error_code);
