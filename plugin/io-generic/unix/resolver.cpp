// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <boost/asio/error.hpp>

#include <rstream/io/detail/stream/url.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::unix_::resolver::resolve_internal(const boost::urls::url& url, boost::asio::local::stream_protocol::endpoint& endpoint, boost::system::error_code& error_code)
{
  bool abstract_socket = false;
  {
    const auto params = rstream::io::detail::stream::url_params(url);
    auto it           = params.find("unix.abstract");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(abstract_socket, *it, error_code);
    }
  }
  if (error_code) {
    return;
  }
  auto path = url.path();
  if (abstract_socket) {
#ifdef __linux__
#ifdef DEBUG_BUILD
    m_logger->trace("abstract socket option set to true");
#endif
    path.insert(0, 1, '\0');
#else
#ifdef DEBUG_BUILD
    m_logger->trace("abstract socket are not supported on this platform");
#endif
    error_code = boost::asio::error::address_family_not_supported;
#endif
  }
  if (error_code) {
    return;
  }
  endpoint = boost::asio::local::stream_protocol::endpoint(path);
}
