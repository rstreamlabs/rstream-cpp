// See LICENSE file in the project root for license information.

#include "stream.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

rstream::core::plugin::element::info stream::get_stream_info()
{
  return rstream::core::plugin::element::info{.m_name = "unix", .m_description = "UNIX sockets transport layer"};
}

}  // namespace unix_
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
