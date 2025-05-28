// See LICENSE file in the project root for license information.

#pragma once

#include <cstdlib>

namespace rstream {
namespace stun {

void hmac_sha1(const void* data, std::size_t size, const void* key, std::size_t key_size, void* dst);

}
}  // namespace rstream
