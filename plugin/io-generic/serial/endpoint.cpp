// See LICENSE file in the project root for license information.

#include "endpoint.hpp"

namespace rstream {
namespace plugin {
namespace io_generic {
namespace serial {

std::ostream& operator<<(std::ostream& ostream, const descriptor& endpoint)
{
  return ostream << "protocol: serial, path: " << endpoint.m_device << ", baudrate: " << endpoint.m_baudrate;
}

}  // namespace serial
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream
