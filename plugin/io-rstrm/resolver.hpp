// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/io-rstrm/resolver.hpp>
#include <rstream/io/detail/stream/resolver_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_rstrm {

using resolver = rstream::io::detail::stream::resolver_impl<rstream::io_rstrm::resolver>;

}
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_rstrm::resolver::async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const rstream::io_rstrm::resolver::results_type&)>&& handler);
