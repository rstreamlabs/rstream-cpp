// See LICENSE file in the project root for license information.

#include "crc32.hpp"

#include <boost/crc.hpp>

namespace rstream {
namespace core {

std::uint32_t crc32(const void* data, std::size_t size)
{
  boost::crc_32_type result;
  result.process_bytes(data, size);
  return result.checksum();
}

}  // namespace core
}  // namespace rstream
