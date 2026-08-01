// See LICENSE file in the project root for license information.

#include "hmac.hpp"

#include <limits>
#include <stdexcept>

#include <openssl/hmac.h>

namespace rstream {
namespace stun {

void hmac_sha1(const void* data, std::size_t size, const void* key, std::size_t key_size, void* dst)
{
  if (!dst) {
    throw std::invalid_argument("HMAC-SHA1 destination is null");
  }
  if (key_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    throw std::length_error("HMAC-SHA1 key is too large");
  }
  unsigned int digest_size = 0;
  if (!::HMAC(
          ::EVP_sha1(),
          key,
          static_cast<int>(key_size),
          static_cast<const unsigned char*>(data),
          size,
          static_cast<unsigned char*>(dst),
          &digest_size)
      || digest_size != 20) {
    throw std::runtime_error("failed to compute HMAC-SHA1 digest");
  }
}

}  // namespace stun
}  // namespace rstream
