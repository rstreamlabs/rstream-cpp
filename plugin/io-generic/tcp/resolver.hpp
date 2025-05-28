// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/ip/tcp.hpp>

#include <rstream/io/detail/stream/resolver_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

using resolver = rstream::io::detail::stream::resolver_impl<boost::asio::ip::tcp::resolver>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::tcp::resolver::async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const boost::asio::ip::tcp::resolver::results_type&)>&& handler);
