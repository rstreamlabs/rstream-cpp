// See LICENSE file in the project root for license information.

#include "acceptor.hpp"

#ifndef _WIN32
#include <boost/filesystem.hpp>

#include <unistd.h>
#endif

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
void rstream::plugin::io_generic::unix_::acceptor::configure_internal(const boost::asio::local::stream_protocol::endpoint& endpoint, const boost::urls::url&, boost::system::error_code& error_code)
{
#ifndef _WIN32
  bool abstract_socket = false;
  {
    const auto& path = endpoint.path();
    if (path.length() > 0 && path[0] == '\0') {
      abstract_socket = true;
    }
  }
  if (!abstract_socket) {
    boost::filesystem::path path(endpoint.path());
    boost::filesystem::create_directories(path.parent_path(), error_code);
    if (error_code) {
#ifdef DEBUG_BUILD
      m_logger->trace("failed to create directories for path: '{}'", path.string());
#endif
    }
    else {
      ::unlink(path.c_str());
    }
  }
#else
  (void)endpoint;
  (void)error_code;
#endif
}
