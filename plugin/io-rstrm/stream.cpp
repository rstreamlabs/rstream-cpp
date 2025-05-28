// See LICENSE file in the project root for license information.

#include "stream.hpp"

namespace rstream {
namespace plugin {
namespace io_rstrm {

rstream::core::plugin::element::info stream::get_stream_info()
{
  return (rstream::core::plugin::element::info){.m_name = "rstrm", .m_description = "rstrm transport layer"};
}

}  // namespace io_rstrm
}  // namespace plugin
}  // namespace rstream
