// See LICENSE file in the project root for license information.

#pragma once

#include <rstream/io-rstrm/endpoint.hpp>
#include <rstream/io/detail/stream/endpoint_impl.hpp>

namespace rstream {
namespace plugin {
namespace io_rstrm {

using endpoint = rstream::io::detail::stream::endpoint_impl<rstream::io_rstrm::endpoint>;

}
}  // namespace plugin
}  // namespace rstream
