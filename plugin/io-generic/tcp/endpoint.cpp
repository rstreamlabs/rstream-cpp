// See LICENSE file in the project root for license information.

#include "endpoint.hpp"

#include <sstream>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace tcp {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
std::string rstream::plugin::io_generic::tcp::endpoint::to_string() const
{
  std::stringstream stringstream;
  stringstream << "protocol: tcp, endpoint: " << m_endpoint;
  return stringstream.str();
}
