// See LICENSE file in the project root for license information.

#include "stream.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

rstream::core::plugin::element::info stream::get_stream_info()
{
  return (rstream::core::plugin::element::info){.m_name = "tcp", .m_description = "TCP transport layer"};
}

}  // namespace tcp
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
