// See LICENSE file in the project root for license information.

#include "resolver.hpp"

#include <string>

#include <rstream/io/detail/stream/url.hpp>
#include <rstream/io/error.hpp>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::serial::resolver::resolve_internal(const boost::urls::url& url, rstream::plugin::io_generic::serial::descriptor& endpoint, boost::system::error_code& error_code)
{
  const auto params = rstream::io::detail::stream::url_params(url);
  auto it           = params.find("serial.baudrate");
  if (it == params.end()) {
#ifdef DEBUG_BUILD
    m_logger->warn("serial baudrate not found in URI");
#endif
    error_code = rstream::io::error::code::invalid_uri;
  }
  else {
    endpoint = rstream::plugin::io_generic::serial::descriptor{
        .m_device   = url.path(),
        .m_baudrate = static_cast<unsigned int>(std::stoul((*it).value)),
    };
  }
}
