// See LICENSE file in the project root for license information.

#include "acceptor.hpp"

#include <rstream/io/detail/stream/url.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::tcp::acceptor::configure_internal(const boost::asio::ip::tcp::endpoint&, const boost::urls::url& url, boost::system::error_code& error_code)
{
  const auto params  = rstream::io::detail::stream::url_params(url);
  bool reuse_address = true;
  if (!error_code) {
    auto it = params.find("tcp.reuse_address");
    if (it != params.end()) {
      rstream::io::detail::stream::parse_url_param_value(reuse_address, *it, error_code);
    }
  }
  if (!error_code) {
#ifdef DEBUG_BUILD
    m_logger->trace("reuse-address socket option set to {}", reuse_address);
#endif
    m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(reuse_address), error_code);
  }
}
