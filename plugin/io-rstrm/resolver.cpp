// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <boost/url.hpp>

namespace rstream {
namespace plugin {
namespace io_rstrm {

}
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_rstrm::resolver::async_resolve_internal(const boost::urls::url& url, rstream::core::completion_handler<void(const boost::system::error_code&, const rstream::io_rstrm::resolver::results_type&)>&& handler)
{
  m_resolver.async_resolve(url, std::move(handler));
}
