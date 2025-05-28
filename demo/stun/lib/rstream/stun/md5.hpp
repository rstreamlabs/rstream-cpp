// See LICENSE file in the project root for license information.

#pragma once

#include <cstdlib>

namespace rstream {
namespace stun {

void md5_sum(const void* data, std::size_t size, void* dst);

}
}  // namespace rstream
