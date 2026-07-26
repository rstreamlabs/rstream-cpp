// See LICENSE file in the project root for license information.

#include "md5.hpp"

#include <stdexcept>

#include <openssl/evp.h>

namespace rstream {
namespace stun {

void md5_sum(const void* data, std::size_t size, void* dst)
{
  if (!dst) {
    throw std::invalid_argument("MD5 destination is null");
  }
  unsigned int digest_size = 0;
  if (::EVP_Digest(data, size, static_cast<unsigned char*>(dst), &digest_size, ::EVP_md5(), nullptr) != 1 || digest_size != 16) {
    throw std::runtime_error("failed to compute MD5 digest");
  }
}

}  // namespace stun
}  // namespace rstream
