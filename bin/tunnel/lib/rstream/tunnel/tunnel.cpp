// See LICENSE file in the project root for license information.

#include "tunnel.hpp"

namespace rstream {
namespace tunnel {

std::ostream& operator<<(std::ostream& os, const status_proxy& status)
{
  os << static_cast<const io_rstrm::status_extd&>(status);
  os << std::endl
     << "  forwarded  : " << status.m_forwarded.value_or("-");
  return os;
}

nlohmann::json& operator<<(nlohmann::json& json, const status_proxy& status)
{
  json << static_cast<const io_rstrm::status_extd&>(status);
  if (status.m_forwarded) {
    json["forwarded"] = *status.m_forwarded;
  }
  return json;
}

}  // namespace tunnel
}  // namespace rstream
