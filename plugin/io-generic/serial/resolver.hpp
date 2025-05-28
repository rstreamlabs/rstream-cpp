// See LICENSE file in the project root for license information.

#pragma once

#include <boost/asio/serial_port.hpp>

#include <rstream/io/detail/stream/resolver_impl.hpp>

#include "endpoint.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

using resolver = rstream::io::detail::stream::basic_resolver_impl<descriptor>;

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::serial::resolver::resolve_internal(const boost::urls::url& url, rstream::plugin::io_generic::serial::descriptor& endpoint, boost::system::error_code& error_code);
