// See LICENSE file in the project root for license information.

#include "md5.hpp"

#include <openssl/md5.h>

namespace rstream {
namespace stun {

void md5_sum(const void* data, std::size_t size, void* dst)
{
  MD5((const unsigned char*)data, size, (unsigned char*)dst);
}

}  // namespace stun
}  // namespace rstream
