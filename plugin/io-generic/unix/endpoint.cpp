// See LICENSE file in the project root for license information.

#include "endpoint.hpp"

#include <sstream>

namespace rstream {
namespace plugin {
namespace io_generic {
namespace unix_ {

}
}  // namespace io_generic
}  // namespace plugin
}  // namespace rstream

template <>
std::string rstream::plugin::io_generic::unix_::endpoint::to_string() const
{
  std::stringstream stringstream;
  stringstream << "protocol: unix, path: " << m_endpoint.path();
  return stringstream.str();
}
