// See LICENSE file in the project root for license information.

#include "endpoint_base.hpp"

namespace rstream {
namespace io {
namespace detail {
namespace stream {

std::ostream& operator<<(std::ostream& ostream, const endpoint_base& endpoint)
{
  return ostream << endpoint.to_string();
}

}  // namespace stream
}  // namespace detail
}  // namespace io
}  // namespace rstream
