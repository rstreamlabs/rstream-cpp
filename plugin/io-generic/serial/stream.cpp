// See LICENSE file in the project root for license information.

#include "stream.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

rstream::core::plugin::element::info stream::get_stream_info()
{
  return (rstream::core::plugin::element::info){.m_name = "serial", .m_description = "serial transport layer"};
}

}  // namespace serial
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
