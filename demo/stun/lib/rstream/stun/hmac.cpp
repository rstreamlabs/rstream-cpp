// See LICENSE file in the project root for license information.

#include "hmac.hpp"

#include <openssl/hmac.h>

namespace rstream {
namespace stun {

void hmac_sha1(const void* data, std::size_t size, const void* key, std::size_t key_size, void* dst)
{
  HMAC(EVP_sha1(), key, key_size, (const unsigned char*)data, size, (unsigned char*)dst, NULL);
}

}  // namespace stun
}  // namespace rstream
